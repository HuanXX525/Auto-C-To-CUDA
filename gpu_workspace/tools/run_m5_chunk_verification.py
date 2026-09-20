#!/usr/bin/env python3
"""M5 recoverable-chunk and summary-mode acceptance matrix.

This test deliberately exercises stateful inputs (sensor RNG/noise, fault
timelines, actuator state, and communication delay/reorder) across chunk
boundaries.  It compares every full-trace chunk size with the one-chunk
reference, then compares summary mode with the same reference.  A separate
mixed batch contains instances that terminate immediately next to instances
that run to timeout; their payloads must match the corresponding single
instance runs.

The script only writes below ``gpu_workspace/build/m5_chunk`` by default and
does not modify production source or the pinned Aircraft-Simu checkout.  A
CUDA-capable host is required to execute it; ``--skip-build`` and
``--skip-cpu`` are provided for focused reruns.
"""

from __future__ import annotations

import argparse
import csv
import filecmp
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable, Sequence


RTOL = 1.0e-10
ATOL = 1.0e-12
FULL_FILES = (
    "summary.json",
    "trajectory.csv",
    "trajectory_diagnostics.csv",
    "event_log.txt",
    "sensor_log.bin",
    "command_log.bin",
)
SUMMARY_FILES = ("summary.json", "event_log.txt")
PACKET_LAYOUT = {
    "sensor_log.bin": (232, 36),
    "command_log.bin": (192, 36),
}


def fail(message: str) -> "NoReturn":
    raise RuntimeError(message)


def run(command: Sequence[str], cwd: Path, label: str) -> None:
    print(f"m5: {label}")
    try:
        subprocess.run(command, cwd=cwd, check=True)
    except subprocess.CalledProcessError as exc:
        fail(f"{label} failed with status {exc.returncode}")


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        fail(f"missing JSON file: {path}")
    except json.JSONDecodeError as exc:
        fail(f"invalid JSON {path}: {exc}")


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def close_value(left: Any, right: Any, label: str) -> None:
    if isinstance(left, (int, bool)) or isinstance(right, (int, bool)):
        if type(left) is not type(right) or left != right:
            fail(f"{label}: {left!r} != {right!r}")
        return
    if isinstance(left, float) or isinstance(right, float):
        if not isinstance(left, (int, float)) or not isinstance(right, (int, float)):
            fail(f"{label}: type mismatch {left!r} != {right!r}")
        if not math.isclose(float(left), float(right), rel_tol=RTOL,
                           abs_tol=ATOL):
            fail(f"{label}: {left!r} != {right!r}")
        return
    if type(left) is not type(right) or left != right:
        fail(f"{label}: {left!r} != {right!r}")


def compare_json(left_path: Path, right_path: Path, label: str) -> None:
    left = read_json(left_path)
    right = read_json(right_path)

    def walk(a: Any, b: Any, path: str) -> None:
        if isinstance(a, dict) and isinstance(b, dict):
            if set(a) != set(b):
                fail(f"{path}: keys differ {set(a) ^ set(b)}")
            for key in sorted(a):
                walk(a[key], b[key], f"{path}.{key}")
            return
        if isinstance(a, list) and isinstance(b, list):
            if len(a) != len(b):
                fail(f"{path}: list lengths differ {len(a)} != {len(b)}")
            for index, (item_a, item_b) in enumerate(zip(a, b)):
                walk(item_a, item_b, f"{path}[{index}]")
            return
        close_value(a, b, path)

    walk(left, right, label)


def compare_csv(left_path: Path, right_path: Path, label: str) -> None:
    with left_path.open(newline="", encoding="utf-8") as left_file:
        left_rows = list(csv.reader(left_file))
    with right_path.open(newline="", encoding="utf-8") as right_file:
        right_rows = list(csv.reader(right_file))
    if len(left_rows) != len(right_rows):
        fail(f"{label}: row count {len(left_rows)} != {len(right_rows)}")
    if not left_rows:
        return
    if left_rows[0] != right_rows[0]:
        fail(f"{label}: headers differ")
    for row_index, (left_row, right_row) in enumerate(
            zip(left_rows[1:], right_rows[1:]), 1):
        if len(left_row) != len(right_row):
            fail(f"{label}: row {row_index} width differs")
        for column, (left_token, right_token) in enumerate(
                zip(left_row, right_row)):
            try:
                left_value = float(left_token)
                right_value = float(right_token)
            except ValueError:
                if left_token != right_token:
                    fail(f"{label}: row {row_index} col {column} differs")
                continue
            if not math.isclose(left_value, right_value, rel_tol=RTOL,
                                abs_tol=ATOL):
                fail(f"{label}: row {row_index} col {column}: "
                     f"{left_value!r} != {right_value!r}")


def normalized_packet_bytes(path: Path) -> bytes:
    """Return packet bytes with only the per-instance header ID zeroed."""
    record_size, _ = PACKET_LAYOUT[path.name]
    data = bytearray(path.read_bytes())
    if len(data) % record_size != 0:
        fail(f"{path}: size is not a whole number of packets")
    for offset in range(0, len(data), record_size):
        # PacketHeader.instance_id is the u32 at wire offset 12.
        data[offset + 12:offset + 16] = b"\0\0\0\0"
    return bytes(data)


def packet_sequences(path: Path) -> list[int]:
    record_size, payload_offset = PACKET_LAYOUT[path.name]
    data = path.read_bytes()
    if len(data) % record_size != 0:
        fail(f"{path}: invalid packet size")
    sequences: list[int] = []
    for offset in range(0, len(data), record_size):
        header_seq = int.from_bytes(data[offset + 16:offset + 20], "little")
        payload_seq = int.from_bytes(
            data[offset + payload_offset:offset + payload_offset + 4], "little")
        if header_seq != payload_seq:
            fail(f"{path}: packet at {offset} has mismatched sequence")
        sequences.append(header_seq)
    return sequences


def compare_instance_payload(left: Path, right: Path, full: bool,
                             label: str) -> None:
    names: Iterable[str] = FULL_FILES if full else SUMMARY_FILES
    for name in names:
        left_path = left / name
        right_path = right / name
        if not left_path.exists() or not right_path.exists():
            fail(f"{label}: missing {name}")
        if name == "summary.json":
            compare_json(left_path, right_path, f"{label}/{name}")
        elif name.endswith(".csv"):
            compare_csv(left_path, right_path, f"{label}/{name}")
        elif name.endswith(".bin"):
            if normalized_packet_bytes(left_path) != normalized_packet_bytes(right_path):
                fail(f"{label}/{name}: packet bytes differ")
        elif not filecmp.cmp(left_path, right_path, shallow=False):
            fail(f"{label}/{name}: bytes differ")

    if full:
        trajectory = left / "trajectory.csv"
        rows = max(0, sum(1 for _ in trajectory.open(encoding="utf-8")) - 1)
        for name in PACKET_LAYOUT:
            packets = packet_sequences(left / name)
            # The initial trace record has no I/O frame. Every later trace
            # record has exactly one sensor/command packet.
            if len(packets) != max(0, rows - 1):
                fail(f"{label}: {name} packet count {len(packets)} "
                     f"does not match trace rows {rows}")


def instance_names(directory: Path) -> list[str]:
    return sorted(path.name for path in directory.glob("instance_[0-9][0-9][0-9][0-9]")
                  if path.is_dir())


def compare_campaign_payload(left: Path, right: Path, full: bool,
                             label: str) -> None:
    left_instances = instance_names(left)
    right_instances = instance_names(right)
    if left_instances != right_instances:
        fail(f"{label}: instance sets differ")
    for name in left_instances:
        compare_instance_payload(left / name, right / name, full,
                                 f"{label}/{name}")
    compare_json(left / "campaign_summary.json",
                 right / "campaign_summary.json", f"{label}/campaign_summary")


def assert_trace_mode(directory: Path, mode: str, chunk: int) -> None:
    execution = read_json(directory / "gpu_execution.json")
    if execution.get("trace_mode") != mode:
        fail(f"{directory}: trace_mode={execution.get('trace_mode')!r}, "
             f"expected {mode!r}")
    if execution.get("chunk_steps") != chunk:
        fail(f"{directory}: chunk_steps={execution.get('chunk_steps')!r}, "
             f"expected {chunk}")


def assert_summary_only(directory: Path) -> None:
    for instance in instance_names(directory):
        path = directory / instance
        for name in ("trajectory.csv", "trajectory_diagnostics.csv",
                     "sensor_log.bin", "command_log.bin"):
            if (path / name).exists():
                fail(f"{directory}: summary mode unexpectedly emitted {name}")


def make_scenario(base: dict[str, Any], max_time: float,
                  hit_radius: float) -> dict[str, Any]:
    scenario = json.loads(json.dumps(base))
    scenario["simulation"]["max_time"] = max_time
    scenario["simulation"]["hit_radius_m"] = hit_radius
    mapping = scenario["map"]
    mapping["enable_terrain"] = False
    mapping["enable_los_occlusion"] = False
    mapping["missing_tile_policy"] = "FLAT_FILL"
    mapping.pop("tile_path", None)
    mapping.pop("tile_paths", None)
    mapping.pop("tile_index_path", None)
    return scenario


def make_runtime(path: Path, campaign_id: str, instances: list[dict[str, Any]],
                 output_dir: Path, port: int) -> None:
    runtime = {
        "schema_version": 1,
        "campaign": {
            "campaign_id": campaign_id,
            "instance_count": len(instances),
            "schedule": "PARALLEL",
            "max_parallel_instances": len(instances),
            "base_random_seed": 12345,
            "failure_strategy": "CONTINUE_ON_FAILURE",
        },
        "network": {
            "protocol_version_major": 1,
            "protocol_version_minor": 0,
            "environment_base_port": port,
            "flight_control_base_port": port + 1,
            "host": "127.0.0.1",
        },
        "logging": {
            "output_dir": str(output_dir),
            "instance_dir_template": "instance_${instance_id}",
            "binary_logs": True,
            "event_log": True,
            "flush_every_steps": 100,
        },
        "tools": {
            "environment_program": "",
            "flight_control_program": "",
        },
        "instances": instances,
    }
    write_json(path, runtime)


def materialize(workspace: Path, root: Path, case_spec: dict[str, Any],
                steps: int) -> dict[str, Any]:
    repo = workspace.parent
    baseline = repo / "external/Aircraft-Simu/configs/baseline"
    source_scenario = read_json(baseline / "scenario.json")
    dt = float(source_scenario["simulation"]["dt"])
    max_time = steps * dt
    root.mkdir(parents=True, exist_ok=True)
    normal_scenario_path = root / "scenario_normal.json"
    early_scenario_path = root / "scenario_early_hit.json"
    write_json(normal_scenario_path, make_scenario(source_scenario, max_time, 0.01))
    write_json(early_scenario_path,
               make_scenario(source_scenario, max_time, 1.0e9))

    def fault_file(name: str, faults: list[dict[str, Any]]) -> Path:
        path = root / f"faults_{name}.json"
        write_json(path, {"schema_version": 1, "faults": faults})
        return path

    rng_faults = fault_file("rng", case_spec["faults"])
    empty_faults = fault_file("empty", [])

    def instance(instance_id: int, scenario: Path, faults: Path,
                 seed: int = 12345) -> dict[str, Any]:
        return {
            "enabled": True,
            "instance_id": instance_id,
            "scenario": str(scenario.resolve()),
            "flight_control": str((baseline / "flight_control.json").resolve()),
            "faults": str(faults.resolve()),
            "random_seed": seed,
        }

    runtimes: dict[str, Any] = {}
    rng_runtime = root / "runtime_rng_faults.json"
    make_runtime(rng_runtime, "m5_rng_faults",
                 [instance(0, normal_scenario_path, rng_faults)],
                 root / "cpu_rng_faults", 59100)
    runtimes["rng_faults"] = rng_runtime

    normal_runtime = root / "runtime_normal.json"
    make_runtime(normal_runtime, "m5_normal",
                 [instance(0, normal_scenario_path, rng_faults)],
                 root / "cpu_normal", 59110)
    runtimes["normal"] = normal_runtime
    early_runtime = root / "runtime_early_single.json"
    make_runtime(early_runtime, "m5_early_single",
                 [instance(0, early_scenario_path, empty_faults)],
                 root / "cpu_early_single", 59120)
    runtimes["early_single"] = early_runtime
    early_batch_runtime = root / "runtime_early_batch.json"
    make_runtime(
        early_batch_runtime, "m5_early_batch",
        [instance(0, early_scenario_path, empty_faults),
         instance(1, normal_scenario_path, rng_faults),
         instance(2, normal_scenario_path, rng_faults),
         instance(3, early_scenario_path, empty_faults)],
        root / "cpu_early_batch", 59130)
    runtimes["early_batch"] = early_batch_runtime
    return {
        "runtimes": runtimes,
        "all_chunk": int(math.ceil(max_time / dt)) + 3,
        "steps": steps,
    }


def run_cpu_baseline(workspace: Path, runtime: Path) -> None:
    binary = workspace / "build/cpu_baseline/instance_manager"
    run([str(binary), "--runtime", str(runtime)], workspace,
        f"CPU baseline {runtime.stem}")


def run_gpu(workspace: Path, runtime: Path, output: Path, mode: str,
            chunk: int) -> None:
    if output.exists():
        shutil.rmtree(output)
    binary = workspace / "build/bin/gpu_campaign"
    command = [str(binary), "--config", str(runtime), "--output-dir", str(output),
               "--trace-mode", mode, "--chunk-steps", str(chunk),
               "--mapping", "packed", "--lanes-per-block", "4"]
    run(command, workspace, f"GPU {runtime.stem} {mode} chunk={chunk}")
    assert_trace_mode(output, mode, chunk)


def compare_with_cpu(repo: Path, cpu_output: Path, gpu_output: Path) -> None:
    compare = repo / "compare_runs.py"
    run([sys.executable, str(compare), str(cpu_output), str(gpu_output),
         "--rtol", str(RTOL), "--atol", str(ATOL)], repo,
        f"CPU/GPU comparison {gpu_output.name}")


def run_full_matrix(workspace: Path, repo: Path, root: Path, runtime: Path,
                    all_chunk: int, cpu_output: Path | None,
                    name: str, chunks: Sequence[int]) -> Path:
    outputs: dict[int, Path] = {}
    reference: Path | None = None
    for chunk in chunks:
        output = root / f"gpu_{name}_full_chunk_{chunk}"
        run_gpu(workspace, runtime, output, "full", chunk)
        outputs[chunk] = output
        if chunk == all_chunk:
            reference = output
    if reference is None:
        fail(f"{name}: all-chunk reference was not run")
    assert reference is not None
    for chunk, output in outputs.items():
        compare_campaign_payload(reference, output, True,
                                 f"{name}/full chunk={chunk}")
    if cpu_output is not None:
        compare_with_cpu(repo, cpu_output, reference)

    # A second identical run verifies deterministic RNG and fault history,
    # rather than merely equality after two different launch schedules.
    repeat = root / f"gpu_{name}_full_chunk_64_repeat"
    run_gpu(workspace, runtime, repeat, "full", 64)
    compare_campaign_payload(outputs[64], repeat, True,
                             f"{name}/repeat chunk=64")
    return reference


def run_summary_matrix(workspace: Path, root: Path, runtime: Path,
                       all_chunk: int, full_reference: Path,
                       name: str, chunks: Sequence[int]) -> None:
    for chunk in chunks:
        output = root / f"gpu_{name}_summary_chunk_{chunk}"
        run_gpu(workspace, runtime, output, "summary", chunk)
        assert_summary_only(output)
        compare_campaign_payload(full_reference, output, False,
                                 f"{name}/summary chunk={chunk}")


def run_early_isolation(workspace: Path, root: Path, runtimes: dict[str, Any],
                        all_chunk: int) -> None:
    # Single-instance references establish the expected state for both an
    # immediately-hit instance and a still-running fault/RNG instance.
    normal_ref = root / "gpu_normal_full_chunk_64"
    early_ref = root / "gpu_early_single_full_chunk_64"
    run_gpu(workspace, runtimes["normal"], normal_ref, "full", 64)
    run_gpu(workspace, runtimes["early_single"], early_ref, "full", 64)
    batch_outputs: list[Path] = []
    for chunk in (1, 64, 256, all_chunk):
        output = root / f"gpu_early_batch_full_chunk_{chunk}"
        run_gpu(workspace, runtimes["early_batch"], output, "full", chunk)
        batch_outputs.append(output)
        names = instance_names(output)
        if names != ["instance_0000", "instance_0001", "instance_0002",
                     "instance_0003"]:
            fail(f"early_batch chunk={chunk}: unexpected instances {names}")
        for instance_id in (0, 3):
            compare_instance_payload(
                early_ref / "instance_0000",
                output / f"instance_{instance_id:04d}", True,
                f"early_batch chunk={chunk} instance={instance_id}")
        for instance_id in (1, 2):
            compare_instance_payload(
                normal_ref / "instance_0000",
                output / f"instance_{instance_id:04d}", True,
                f"early_batch chunk={chunk} instance={instance_id}")
    for output in batch_outputs[1:]:
        compare_campaign_payload(batch_outputs[0], output, True,
                                 f"early_batch cross-chunk {output.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-cpu", action="store_true",
                        help="skip CPU baseline and CPU/GPU comparisons")
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    repo = workspace.parent
    root = (args.output or workspace / "build/m5_chunk").resolve()
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)

    if not args.skip_build:
        run([str(workspace / "tools/build_cpu_baseline.sh")], workspace,
            "build CPU baseline")
        run(["make", "-C", str(workspace), "-j2"], workspace,
            "build GPU campaign")

    specification = read_json(workspace / "configs/m5/cases.json")
    cases = {case["name"]: case for case in specification["cases"]}
    steps = int(specification["steps"])
    chunk_values = [int(value) for value in specification["chunk_steps"]
                    if value != "all"]
    prepared = materialize(workspace, root, cases["rng_faults"], steps)
    all_chunk = int(prepared["all_chunk"])
    chunks = chunk_values + [all_chunk]
    if len(set(chunks)) != len(chunks):
        fail(f"duplicate chunk sizes after all expansion: {chunks}")

    runtimes = prepared["runtimes"]
    if not args.skip_cpu:
        for key in ("rng_faults", "normal", "early_single"):
            run_cpu_baseline(workspace, runtimes[key])

    rng_cpu = None if args.skip_cpu else root / "cpu_rng_faults"
    rng_reference = run_full_matrix(
        workspace, repo, root, runtimes["rng_faults"], all_chunk, rng_cpu,
        "rng_faults", chunks)
    run_summary_matrix(workspace, root, runtimes["rng_faults"], all_chunk,
                       rng_reference, "rng_faults", chunks)
    run_early_isolation(workspace, root, runtimes, all_chunk)
    print("m5_chunk_verification: PASS chunk=1/64/256/all, full/summary, "
          "RNG/fault/communication continuity, early-termination isolation")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"m5_chunk_verification: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
