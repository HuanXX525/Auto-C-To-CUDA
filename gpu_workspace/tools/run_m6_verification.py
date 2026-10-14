#!/usr/bin/env python3
"""M6 production CLI and CPU-compatible output acceptance.

The test materializes a copy of the tracked baseline runtime with absolute
references, runs the unchanged CPU instance manager and the CUDA campaign,
then verifies the full and summary output contracts with ``compare_runs.py``.
All generated files stay below ``gpu_workspace/build/m6_production``.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, NoReturn, Sequence


FULL_FILES = {
    "run_manifest.json",
    "trajectory.csv",
    "trajectory_diagnostics.csv",
    "summary.json",
    "event_log.txt",
    "sensor_log.bin",
    "command_log.bin",
}
SUMMARY_FILES = {"run_manifest.json", "summary.json", "event_log.txt"}


def fail(message: str) -> NoReturn:
    raise RuntimeError(message)


def run(command: Sequence[str], cwd: Path, label: str) -> None:
    print(f"m6: {label}")
    try:
        subprocess.run(command, cwd=cwd, check=True)
    except subprocess.CalledProcessError as exc:
        fail(f"{label} failed with status {exc.returncode}")


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError) as exc:
        fail(f"cannot read JSON {path}: {exc}")


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def materialize_runtime(workspace: Path, root: Path) -> Path:
    repo = workspace.parent
    source = workspace / "configs/runtime_1.json"
    runtime = read_json(source)
    runtime["logging"]["output_dir"] = str((root / "cpu").resolve())
    runtime["network"]["environment_base_port"] = 59200
    runtime["network"]["flight_control_base_port"] = 59201
    runtime["tools"] = {
        "environment_program": str(
            (workspace / "build/cpu_baseline/environment_sim").resolve()),
        "flight_control_program": str(
            (workspace / "build/cpu_baseline/flight_control_sim").resolve()),
    }
    for instance in runtime["instances"]:
        for key, fallback in (
            ("scenario", repo),
            ("flight_control", repo),
            ("faults", repo),
        ):
            value = Path(instance[key])
            if not value.is_absolute():
                candidate = (fallback / value).resolve()
                if not candidate.exists():
                    candidate = (source.parent / value).resolve()
                value = candidate
            instance[key] = str(value)
    runtime_path = root / "runtime_m6.json"
    write_json(runtime_path, runtime)
    return runtime_path


def instance_files(directory: Path) -> set[str]:
    instances = sorted(directory.glob("instance_[0-9][0-9][0-9][0-9]"))
    if not instances:
        fail(f"no instance directories in {directory}")
    names = {path.name for path in instances[0].iterdir() if path.is_file()}
    for instance in instances[1:]:
        current = {path.name for path in instance.iterdir() if path.is_file()}
        if current != names:
            fail(f"instance file sets differ in {directory}")
    return names


def assert_outputs(full: Path, summary: Path) -> None:
    full_names = instance_files(full)
    summary_names = instance_files(summary)
    missing = FULL_FILES - full_names
    if missing:
        fail(f"full output missing files: {sorted(missing)}")
    unexpected = summary_names - SUMMARY_FILES
    if unexpected:
        fail(f"summary output emitted full-trace files: {sorted(unexpected)}")
    for directory, expected_mode in ((full, "full"), (summary, "summary")):
        metadata = read_json(directory / "gpu_execution.json")
        if metadata.get("trace_mode") != expected_mode:
            fail(f"{directory}: unexpected trace mode {metadata.get('trace_mode')!r}")
    full_metadata = read_json(full / "gpu_execution.json")
    if full_metadata.get("mapping") != "packed" or full_metadata.get("lanes_per_block") != 4:
        fail("auto mapping did not select packed/4 for N=1")
    summary_metadata = read_json(summary / "gpu_execution.json")
    if summary_metadata.get("mapping") != "serial":
        fail("explicit serial mapping was not recorded")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-cpu", action="store_true")
    args = parser.parse_args()

    workspace = args.workspace.resolve()
    repo = workspace.parent
    root = (args.output or workspace / "build/m6_production").resolve()
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)

    if not args.skip_build:
        run([str(workspace / "tools/build_cpu_baseline.sh")], repo,
            "build CPU baseline")
        run(["make", "-C", str(workspace), "-j2"], repo,
            "build GPU campaign")

    runtime = materialize_runtime(workspace, root)
    cpu_output = root / "cpu"
    full_output = root / "gpu_full"
    summary_output = root / "gpu_summary"
    cpu_manager = workspace / "build/cpu_baseline/instance_manager"
    gpu_binary = workspace / "build/bin/gpu_campaign"
    if not args.skip_cpu:
        run([str(cpu_manager), "--runtime", str(runtime)], repo,
            "CPU baseline")

    # Exercise the production parser's mutual-exclusion rule before launching
    # any kernel.  A legacy probe option must never be silently ignored.
    mixed = subprocess.run(
        [str(gpu_binary), "--config", str(runtime), "--steps", "8"],
        cwd=repo, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, check=False)
    if mixed.returncode == 0 or "cannot be combined" not in mixed.stderr:
        fail("--config accepted a mixed legacy probe option")

    run([str(gpu_binary), "--config", str(runtime),
         "--output-dir", str(full_output), "--trace-mode", "full",
         "--chunk-steps", "64", "--mapping", "auto",
         "--lanes-per-block", "4"], Path("/tmp"),
        "GPU full production run")
    run([str(gpu_binary), "--config", str(runtime),
         "--output-dir", str(summary_output), "--trace-mode", "summary",
         "--chunk-steps", "64", "--mapping", "serial",
         "--lanes-per-block", "4"], Path("/tmp"),
        "GPU summary production run")
    assert_outputs(full_output, summary_output)

    compare = repo / "compare_runs.py"
    if not args.skip_cpu:
        run([sys.executable, str(compare), str(cpu_output), str(full_output),
             "--rtol", "1e-10", "--atol", "1e-12"], repo,
            "CPU/GPU full semantic comparison")
    run([sys.executable, str(compare), str(full_output), str(summary_output),
         "--rtol", "1e-10", "--atol", "1e-12"], repo,
        "full/summary final-state comparison")
    print("m6_verification: PASS production CLI, CPU-compatible full output, "
          "summary output, semantic comparison")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"m6_verification: FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
