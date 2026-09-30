#!/usr/bin/env python3
"""Close M7 from immutable segment receipts and focused M5/M6 regressions.

This is deliberately an aggregation gate, not another M7 matrix run.  It
validates the formal B--E evidence, proves that the evidence still describes
the current protected sources and binaries, runs the focused M5/M6 regression
gates with CPU comparisons enabled, and publishes byte-identical local and
tracked closure receipts.  It never removes an existing output.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, NoReturn, Sequence


SECTION_ORDER = ("short", "mapping", "chunk", "batch", "long", "errors")
SEGMENTS = (
    ("M7-B", "m7_b_functional", ("short", "errors")),
    ("M7-C", "m7_c_invariance", ("mapping", "chunk")),
    ("M7-D", "m7_d_batch", ("batch",)),
    ("M7-E", "m7_e_long", ("long",)),
)
AIRCRAFT_COMMIT = "970bdfbe97db21aafb6cd94afee0ecbe72c8544c"
M7_ANCHORS = (
    ("M7-A", "e037639bb49c6219431a7752c21205674df76765"),
    ("M7-B", "9823707a0366b8a9b13b684d840aa08ba2d9a2cd"),
    ("M7-C", "28ae2d33089d3cd708067a35382bbee29c6b4895"),
    ("M7-D", "308897abecadf7a3ffaee4ff81a875877961544e"),
    ("M7-E calibration", "f168c9d57525b8c776aa62981f73bd208e2148f6"),
    ("M7-E record", "900ed4c7064a00de4325beabb877d49689f3b648"),
)
CALIBRATION_COMMIT = M7_ANCHORS[-2][1]
ALLOWED_POST_CALIBRATION_PATHS = frozenset({
    "planA.md",
    "gpu_workspace/README.md",
    "gpu_workspace/tools/run_m7_closure.py",
    "gpu_workspace/validation/correctness_m7_progress.json",
    "gpu_workspace/validation/m7_receipts/m7_b_functional.json",
    "gpu_workspace/validation/m7_receipts/m7_b_functional_pre_velocity_calibration.json",
    "gpu_workspace/validation/m7_receipts/m7_c_invariance.json",
    "gpu_workspace/validation/m7_receipts/m7_c_invariance_pre_velocity_calibration.json",
    "gpu_workspace/validation/m7_receipts/m7_d_batch.json",
    "gpu_workspace/validation/m7_receipts/m7_d_batch_pre_velocity_calibration.json",
    "gpu_workspace/validation/m7_receipts/m7_e_long.json",
})
SOURCE_PATHS = {
    "runner": "gpu_workspace/tools/run_m7_correctness_matrix.py",
    "matrix": "gpu_workspace/configs/m7/cases.json",
    "comparator": "compare_runs.py",
    "m4_asset_generator": "gpu_workspace/tools/prepare_m4_cases.py",
    "m4_case_matrix": "gpu_workspace/configs/m4/cases.json",
    "terrain_tile_generator": "gpu_workspace/host_tests/terrain_tile_generator.cpp",
    "cpu_build_script": "gpu_workspace/tools/build_cpu_baseline.sh",
    "gpu_makefile": "gpu_workspace/Makefile",
    "baseline_scenario": "external/Aircraft-Simu/configs/baseline/scenario.json",
    "baseline_flight_control": "external/Aircraft-Simu/configs/baseline/flight_control.json",
}
BINARY_PATHS = {
    "gpu_campaign": "gpu_workspace/build/bin/gpu_campaign",
    "cpu_environment": "gpu_workspace/build/cpu_baseline/environment_sim",
    "cpu_flight_control": "gpu_workspace/build/cpu_baseline/flight_control_sim",
    "cpu_instance_manager": "gpu_workspace/build/cpu_baseline/instance_manager",
}


class ClosureFailure(RuntimeError):
    pass


def fail(message: str) -> NoReturn:
    raise ClosureFailure(message)


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError) as exc:
        fail(f"cannot read JSON {path}: {exc}")
    if not isinstance(value, dict):
        fail(f"JSON root must be an object: {path}")
    return value


def sha256_file(path: Path) -> str:
    if not path.is_file():
        fail(f"required file is missing: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_fingerprint(base: Path, roots: Sequence[Path]) -> dict[str, Any]:
    files: set[Path] = set()
    for root in roots:
        if root.is_file():
            files.add(root.resolve())
        elif root.is_dir():
            files.update(path.resolve() for path in root.rglob("*")
                         if path.is_file() and not path.name.endswith(".tmp"))
    digest = hashlib.sha256()
    total_bytes = 0
    for path in sorted(files, key=lambda item: item.as_posix()):
        try:
            relative = path.relative_to(base).as_posix()
        except ValueError:
            relative = path.as_posix()
        size = path.stat().st_size
        total_bytes += size
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\0")
        digest.update(sha256_file(path).encode("ascii"))
        digest.update(b"\n")
    return {"sha256": digest.hexdigest(), "file_count": len(files),
            "total_bytes": total_bytes}


def git(repo: Path, arguments: Sequence[str], label: str) -> str:
    result = subprocess.run(["git", "-C", str(repo), *arguments], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            check=False)
    if result.returncode != 0:
        fail(f"{label} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def run(command: Sequence[str], cwd: Path, label: str) -> None:
    print(f"m7_closure: {label}", flush=True)
    result = subprocess.run(list(command), cwd=cwd, check=False)
    if result.returncode != 0:
        fail(f"{label} failed with status {result.returncode}")


def require_equal(label: str, actual: Any, expected: Any) -> None:
    if actual != expected:
        fail(f"{label} mismatch: expected {expected!r}, got {actual!r}")


def current_hashes(repo: Path, paths: dict[str, str]) -> dict[str, str]:
    return {name: sha256_file(repo / relative)
            for name, relative in paths.items()}


def validate_artifacts(workspace: Path, name: str,
                       receipt: dict[str, Any]) -> None:
    root = workspace / "build" / name
    artifacts = receipt.get("artifacts", {})
    actual = {
        "gpu_execution_count": len(list(root.rglob("gpu_execution.json"))),
        "cpu_campaign_count": len(list((root / "cpu").glob(
            "*/campaign_summary.json"))),
        "gpu_campaign_count": len(list((root / "gpu").glob(
            "*/campaign_summary.json"))),
        "generated_inputs": tree_fingerprint(
            root, [root / "runtimes", root / "scenarios", root / "faults",
                   root / "m4_assets"]),
        "results": tree_fingerprint(
            root, [root / "cpu", root / "gpu", root / "errors"]),
    }
    require_equal(f"{name} artifact fingerprints", actual, artifacts)


def progress_receipt_hashes(progress: dict[str, Any]) -> dict[str, str]:
    entries = list(progress.get("completed_segment_history", ()))
    entries.append(progress.get("current_segment", {}))
    hashes: dict[str, str] = {}
    for entry in entries:
        segment = entry.get("id")
        if segment in {item[0] for item in SEGMENTS}:
            require_equal(f"{segment} progress status", entry.get("status"), "PASS")
            value = entry.get("receipt_sha256")
            if not isinstance(value, str) or len(value) != 64:
                fail(f"{segment} progress record has no valid receipt SHA-256")
            hashes[segment] = value
    require_equal("progress receipt segments", set(hashes),
                  {item[0] for item in SEGMENTS})
    return hashes


def validate_receipts(workspace: Path, repo: Path,
                      expected_hashes: dict[str, str]) -> tuple[
        list[dict[str, Any]], dict[str, Any]]:
    receipts: list[dict[str, Any]] = []
    seen: set[str] = set()
    shared: dict[str, Any] | None = None
    for segment, name, expected_sections in SEGMENTS:
        tracked = workspace / "validation/m7_receipts" / f"{name}.json"
        local = workspace / "build" / name / "m7_pass_receipt.json"
        if tracked.read_bytes() != local.read_bytes():
            fail(f"{segment} tracked and build-local receipts are not byte-identical")
        receipt_hash = sha256_file(tracked)
        require_equal(f"{segment} tracked receipt SHA-256", receipt_hash,
                      expected_hashes[segment])
        receipt = read_json(tracked)
        require_equal(f"{segment} status", receipt.get("status"), "PASS")
        require_equal(f"{segment} phase", receipt.get("phase"), "M7")
        require_equal(f"{segment} complete gate", receipt.get("complete_m7_gate"), False)
        sections = tuple(receipt.get("sections", ()))
        require_equal(f"{segment} sections", sections, expected_sections)
        overlap = seen.intersection(sections)
        if overlap:
            fail(f"{segment} repeats sections: {sorted(overlap)}")
        seen.update(sections)
        source = receipt.get("source", {})
        require_equal(f"{segment} Aircraft commit",
                      source.get("aircraft_simu_commit"), AIRCRAFT_COMMIT)
        require_equal(f"{segment} Aircraft clean marker",
                      source.get("aircraft_simu_tracked_worktree_clean"), True)
        common = {
            "source_sha256": source.get("sha256"),
            "binary_sha256": source.get("binary_sha256"),
            "tolerance": receipt.get("tolerance"),
            "hardware": receipt.get("hardware"),
            "cuda_runtime_versions": receipt.get("cuda_runtime_versions"),
            "parent_commit": source.get("parent_commit"),
        }
        if shared is None:
            shared = common
        else:
            require_equal(f"{segment} shared evidence", common, shared)
        validate_artifacts(workspace, name, receipt)
        receipts.append({
            "segment": segment,
            "sections": list(sections),
            "tracked_path": tracked.relative_to(repo).as_posix(),
            "build_local_path": local.relative_to(repo).as_posix(),
            "sha256": receipt_hash,
            "created_at_utc": receipt.get("created_at_utc"),
            "artifacts": receipt.get("artifacts"),
        })
    require_equal("M7 section coverage", seen, set(SECTION_ORDER))
    assert shared is not None
    require_equal("shared receipt parent", shared["parent_commit"],
                  CALIBRATION_COMMIT)
    return receipts, shared


def validate_ancestry(repo: Path) -> dict[str, Any]:
    head = git(repo, ["rev-parse", "HEAD"], "resolve parent HEAD")
    positions: list[int] = []
    history = git(repo, ["rev-list", "--reverse", M7_ANCHORS[0][1] + "^.." + head],
                  "read M7 ancestry").splitlines()
    parent_rows = git(repo, ["rev-list", "--parents", M7_ANCHORS[0][1] + "^.." + head],
                      "inspect M7 parent graph").splitlines()
    if any(len(row.split()) > 2 for row in parent_rows):
        fail("M7 ancestry contains a merge commit; expected a strict linear chain")
    expected_history = [commit for _, commit in M7_ANCHORS]
    require_equal("exact M7 commit history", history, expected_history)
    for label, commit in M7_ANCHORS:
        if commit not in history:
            fail(f"{label} commit is not on the current ancestry: {commit}")
        positions.append(history.index(commit))
    if positions != sorted(positions) or len(set(positions)) != len(positions):
        fail("M7 anchor commits are not in strict A-through-E order")
    changed = set(filter(None, git(
        repo, ["diff", "--name-only", CALIBRATION_COMMIT + ".." + head],
        "audit post-calibration paths").splitlines()))
    unexpected = changed - ALLOWED_POST_CALIBRATION_PATHS
    if unexpected:
        fail("unexpected paths changed after the M7-E calibration: " +
             ", ".join(sorted(unexpected)))
    return {
        "head": head,
        "anchors": [{"name": label, "commit": commit}
                    for label, commit in M7_ANCHORS],
        "strict_linear": True,
        "post_calibration_base": CALIBRATION_COMMIT,
        "post_calibration_changed_paths": sorted(changed),
        "allowed_paths": sorted(ALLOWED_POST_CALIBRATION_PATHS),
    }


def validate_repository(workspace: Path, repo: Path,
                        shared: dict[str, Any]) -> dict[str, Any]:
    aircraft = repo / "external/Aircraft-Simu"
    require_equal("current Aircraft-Simu commit",
                  git(aircraft, ["rev-parse", "HEAD"], "resolve Aircraft commit"),
                  AIRCRAFT_COMMIT)
    dirty = git(aircraft, ["status", "--porcelain"], "inspect Aircraft worktree")
    if dirty:
        fail("Aircraft-Simu worktree is not clean")
    source_hashes = current_hashes(repo, SOURCE_PATHS)
    require_equal("current protected source hashes", source_hashes,
                  shared["source_sha256"])
    protected_dirty = git(repo, ["status", "--porcelain", "--untracked-files=all",
                                 "--", *SOURCE_PATHS.values()],
                          "inspect protected source paths")
    if protected_dirty:
        fail("protected source paths have uncommitted changes: " + protected_dirty)
    return {
        "aircraft_simu_commit": AIRCRAFT_COMMIT,
        "aircraft_simu_worktree_clean": True,
        "sha256": source_hashes,
        "closure_runner_sha256": sha256_file(Path(__file__).resolve()),
    }


def validate_binaries(repo: Path, shared: dict[str, Any]) -> dict[str, str]:
    hashes = current_hashes(repo, BINARY_PATHS)
    require_equal("current protected binary hashes", hashes,
                  shared["binary_sha256"])
    return hashes


def validate_support_environment(output: Path, shared: dict[str, Any],
                                 label: str) -> dict[str, Any]:
    paths = sorted(output.rglob("gpu_execution.json"))
    if not paths:
        fail(f"{label} emitted no gpu_execution.json evidence")
    executions = [read_json(path) for path in paths]
    hardware = sorted({item.get("gpu") for item in executions if item.get("gpu")})
    cuda_versions = sorted({item.get("cuda_runtime_version") for item in executions
                            if item.get("cuda_runtime_version") is not None})
    require_equal(f"{label} GPU hardware", hardware, shared["hardware"])
    require_equal(f"{label} CUDA runtime", cuda_versions,
                  shared["cuda_runtime_versions"])
    return {"gpu_execution_count": len(paths), "hardware": hardware,
            "cuda_runtime_versions": cuda_versions}


def atomic_publish(path: Path, payload: bytes) -> None:
    if path.exists():
        fail(f"refusing to replace existing receipt: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    if temporary.exists():
        fail(f"refusing to replace stale temporary receipt: {temporary}")
    with temporary.open("xb") as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    workspace = args.workspace.resolve()
    repo = workspace.parent
    build_root = (workspace / "build").resolve()
    root = (args.output or build_root / "m7_f_closure").resolve()
    tracked = (workspace / "validation/correctness_m7.json").resolve()
    try:
        root.relative_to(build_root)
    except ValueError:
        fail(f"closure output must be below {build_root}: {root}")
    if root.parent != build_root or not root.name.startswith("m7_f_"):
        fail("closure output must be a direct build child named m7_f_*")
    if root.exists():
        fail(f"closure output already exists; no files were removed: {root}")
    if tracked != (workspace / "validation/correctness_m7.json").resolve():
        fail("internal tracked receipt path contract is invalid")
    if tracked.exists():
        fail(f"tracked closure receipt already exists: {tracked}")

    progress = read_json(workspace / "validation/correctness_m7_progress.json")
    expected_receipt_hashes = progress_receipt_hashes(progress)
    receipts, shared = validate_receipts(
        workspace, repo, expected_receipt_hashes)
    ancestry = validate_ancestry(repo)
    baseline_parent = progress.get("source", {}).get("baseline_parent_commit")
    require_equal(
        "M7 baseline parent commit", baseline_parent,
        git(repo, ["rev-parse", M7_ANCHORS[0][1] + "^"],
            "resolve M7 baseline parent"))
    ancestry["baseline_parent_commit"] = baseline_parent
    source = validate_repository(workspace, repo, shared)
    if not args.skip_build:
        run([str(workspace / "tools/build_cpu_baseline.sh")], repo,
            "build CPU baseline once")
        run(["make", "-C", str(workspace), "-j2"], repo,
            "build GPU campaign once")
    binaries_before = validate_binaries(repo, shared)

    root.mkdir(parents=False)
    m5_output = root / "m5_regression"
    m6_output = root / "m6_regression"
    run([sys.executable, str(workspace / "tools/run_m5_chunk_verification.py"),
         "--workspace", str(workspace), "--output", str(m5_output),
         "--skip-build"], repo, "run M5 regression with CPU comparisons")
    run([sys.executable, str(workspace / "tools/run_m6_verification.py"),
         "--workspace", str(workspace), "--output", str(m6_output),
         "--skip-build"], repo, "run M6 regression with CPU comparisons")

    binaries_after = validate_binaries(repo, shared)
    source_after = current_hashes(repo, SOURCE_PATHS)
    require_equal("protected source hashes after regressions", source_after,
                  shared["source_sha256"])
    m5_environment = validate_support_environment(
        m5_output, shared, "M5 supporting regression")
    m6_environment = validate_support_environment(
        m6_output, shared, "M6 supporting regression")
    m5_command = [
        "python3", "gpu_workspace/tools/run_m5_chunk_verification.py",
        "--workspace", "gpu_workspace", "--output",
        root.relative_to(repo).as_posix() + "/m5_regression", "--skip-build",
    ]
    m6_command = [
        "python3", "gpu_workspace/tools/run_m6_verification.py",
        "--workspace", "gpu_workspace", "--output",
        root.relative_to(repo).as_posix() + "/m6_regression", "--skip-build",
    ]
    support = {
        "m5": {
            "status": "PASS",
            "command": m5_command,
            "runner_sha256": sha256_file(
                workspace / "tools/run_m5_chunk_verification.py"),
            "cpu_comparisons_skipped": False,
            "output": m5_output.relative_to(repo).as_posix(),
            "artifacts": tree_fingerprint(root, [m5_output]),
            "environment": m5_environment,
        },
        "m6": {
            "status": "PASS",
            "command": m6_command,
            "runner_sha256": sha256_file(
                workspace / "tools/run_m6_verification.py"),
            "cpu_comparisons_skipped": False,
            "output": m6_output.relative_to(repo).as_posix(),
            "artifacts": tree_fingerprint(root, [m6_output]),
            "environment": m6_environment,
        },
    }
    aggregate = {
        key: sum(int(item["artifacts"][key]) for item in receipts)
        for key in ("gpu_execution_count", "cpu_campaign_count",
                    "gpu_campaign_count")
    }
    for category in ("generated_inputs", "results"):
        aggregate[category] = {
            key: sum(int(item["artifacts"][category][key]) for item in receipts)
            for key in ("file_count", "total_bytes")
        }
    canonical_command = ["python3", "gpu_workspace/tools/run_m7_closure.py"]
    if args.output is not None:
        canonical_command.extend(["--output", root.relative_to(repo).as_posix()])
    if args.skip_build:
        canonical_command.append("--skip-build")
    source.update({
        "baseline_parent_commit": baseline_parent,
        "closure_parent_commit": ancestry["head"],
        "receipt_parent_commit": shared["parent_commit"],
    })
    closure = {
        "schema_version": 1,
        "phase": "M7",
        "status": "PASS",
        "complete_m7_gate": True,
        "m7_complete": True,
        "m8_started": False,
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "command": canonical_command,
        "source": source,
        "coverage": {
            "required_sections": list(SECTION_ORDER),
            "covered_sections": list(SECTION_ORDER),
            "complete": True,
            "disjoint_segment_coverage": True,
        },
        "segment_receipts": receipts,
        "consistency": {
            "source_sha256_identical": True,
            "binary_sha256_identical": True,
            "tolerance_identical": True,
            "hardware_identical": True,
            "cuda_runtime_versions_identical": True,
            "tracked_and_build_local_byte_identical": True,
            "artifacts_recomputed": True,
            "binary_sha256_before_regressions": binaries_before,
            "binary_sha256_after_regressions": binaries_after,
        },
        "ancestry": ancestry,
        "supporting_regressions": support,
        "tolerance": shared["tolerance"],
        "hardware": shared["hardware"],
        "cuda_runtime_versions": shared["cuda_runtime_versions"],
        "aggregate_counts": aggregate,
        "superseded_receipts": progress.get("superseded_receipts", []),
    }
    payload = (json.dumps(closure, indent=2) + "\n").encode("utf-8")
    local = root / "m7_closure_receipt.json"
    atomic_publish(local, payload)
    atomic_publish(tracked, payload)
    if local.read_bytes() != tracked.read_bytes():
        fail("published closure receipts are not byte-identical")
    print("m7_closure: PASS M7 evidence aggregation and M5/M6 regressions")
    print(f"m7_closure: local_receipt={local}")
    print(f"m7_closure: tracked_receipt={tracked}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ClosureFailure as exc:
        print(f"m7_closure: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
