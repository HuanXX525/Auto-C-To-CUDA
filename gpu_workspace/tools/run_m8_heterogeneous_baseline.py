#!/usr/bin/env python3
"""M8-A3: frozen heterogeneous N=128, 12000-step correctness gate.

CPU emits full traces; CUDA emits summaries.  GPU primary/repeat, packed/32,
and chunk=64 runs test determinism, mapping independence and chunk continuity.
No previous output or receipt is replaced.  The M8-A2 calibration is inapplicable
unless *all* frozen scenario/control/fault hashes match (they do not here).
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import shutil
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

import run_m8_long_baseline as common


COUNT = 128
PROFILES = (
    "quiet", "noise", "dropout", "sensor_faults", "actuator_faults",
    "communication_delay", "communication_reorder", "fault_overlap_recovery",
)
GPU_VARIANTS = {
    "primary": ("packed", 4, 256),
    "repeat": ("packed", 4, 256),
    "packed32": ("packed", 32, 256),
    "chunk64": ("packed", 4, 64),
}


def validate_input(repo: Path, workspace: Path) -> dict:
    source = workspace / "configs/m8/generated/runtimes/heterogeneous_n128.json"
    runtime = common.read_json(source)
    common.require_equal("heterogeneous count", runtime["campaign"]["instance_count"], COUNT)
    common.require_equal("heterogeneous rows", len(runtime["instances"]), COUNT)
    common.require_equal("CPU schedule", runtime["campaign"]["schedule"], "PARALLEL")
    common.require_equal("CPU concurrency", runtime["campaign"]["max_parallel_instances"], 6)
    common.require_equal("binary logs", runtime["logging"]["binary_logs"], True)
    hashes = []
    profiles = Counter()
    policy = common.load_calibration_policy(repo, workspace)
    scope = policy["scope"]
    calibration_matches = []
    for index, item in enumerate(runtime["instances"]):
        common.require_equal(f"instance {index} ID", item["instance_id"], index)
        common.require_equal(f"instance {index} seed", item["random_seed"], 12345 + 17 * index)
        profile = PROFILES[index % len(PROFILES)]
        profiles[profile] += 1
        expected_scenario = (
            f"gpu_workspace/configs/m8/generated/scenarios/heterogeneous/scenario_{index:04d}.json")
        common.require_equal(f"instance {index} scenario path", item["scenario"], expected_scenario)
        expected_fault = profile if profile in PROFILES[3:] else "empty"
        common.require_equal(f"instance {index} faults", item["faults"],
                             f"gpu_workspace/configs/m8/generated/faults/{expected_fault}.json")
        scenario_sha = common.sha256_file(repo / item["scenario"])
        hashes.append(scenario_sha)
        if (index == scope["instance_id"] and
            item["random_seed"] == scope["random_seed"] and
            scenario_sha == scope["scenario_sha256"] and
            common.sha256_file(repo / item["flight_control"]) == scope["flight_control_sha256"] and
            common.sha256_file(repo / item["faults"]) == scope["faults_sha256"]):
            calibration_matches.append(index)
    common.require_equal("distinct physical scenarios", len(set(hashes)), COUNT)
    common.require_equal("profile distribution", profiles,
                         Counter({name: 16 for name in PROFILES}))
    # A2 applies only to a byte-identical input, never merely a matching seed.
    common.require_equal("A2 calibration scope in heterogeneous input", calibration_matches, [])
    return {"runtime": runtime, "source": source, "profiles": dict(profiles),
            "calibration_matches": calibration_matches,
            "input_sha256": common.sha256_file(source),
            "scenario_tree": common.tree_fingerprint(repo, [
                workspace / "configs/m8/generated/scenarios/heterogeneous"])}


def validate_output(directory: Path, gpu: bool, chunk: int = 256,
                    lanes: int = 4) -> dict:
    common.require_equal("instance output count",
        len(list(directory.glob("instance_[0-9][0-9][0-9][0-9]"))), COUNT)
    campaign = common.read_json(directory / "campaign_summary.json")
    for field, expected in (("instance_count", COUNT), ("completed_count", COUNT),
                            ("failed_count", 0), ("summary_available_count", COUNT)):
        common.require_equal(f"{directory.name} {field}", campaign[field], expected)
    common.require_equal("campaign rows", len(campaign["instances"]), COUNT)
    reasons = Counter()
    fault_profiles = Counter()
    for index in range(COUNT):
        item = directory / f"instance_{index:04d}"
        manifest = common.read_json(item / "run_manifest.json")
        summary = common.read_json(item / "summary.json")
        common.require_equal(f"instance {index} ID", manifest["instance_id"], index)
        common.require_equal(f"instance {index} seed", manifest["random_seed"], 12345 + 17 * index)
        common.require_equal(f"instance {index} dt", float(manifest["dt_s"]), common.DT_S)
        common.require_equal(f"instance {index} max_time", float(manifest["max_time_s"]), 120.0)
        steps = summary["simulation_steps"]
        if not isinstance(steps, int) or not 0 < steps <= common.STEPS:
            common.fail(f"invalid terminal step instance {index}: {steps!r}")
        reason = summary["exit_reason"]
        if reason not in ("timeout", "surface_collision", "hit"):
            common.fail(f"unexpected terminal reason instance {index}: {reason!r}")
        if reason == "timeout" and steps != common.STEPS:
            common.fail(f"early timeout instance {index}: {steps}")
        reasons[reason] += 1
        common.require_equal(f"instance {index} diagnostic samples",
                             summary["diagnostic_sample_count"], steps + 1)
        common.require_equal(f"instance {index} campaign exit",
                             campaign["instances"][index]["exit_reason"], reason)
        common.require_equal(f"instance {index} env status",
                             campaign["instances"][index]["env_status"], 0)
        common.require_equal(f"instance {index} fc status",
                             campaign["instances"][index]["fc_status"], 0)
        if not (item / "event_log.txt").is_file():
            common.fail(f"missing event log instance {index}")
        if gpu:
            for filename in ("trajectory.csv", "trajectory_diagnostics.csv",
                             "sensor_log.bin", "command_log.bin"):
                if (item / filename).exists():
                    common.fail(f"summary mode emitted {item / filename}")
        else:
            for filename in ("trajectory.csv", "trajectory_diagnostics.csv",
                             "sensor_log.bin", "command_log.bin"):
                if not (item / filename).is_file():
                    common.fail(f"missing CPU full output {item / filename}")
        if summary["fault_configured_count"]:
            fault_profiles[PROFILES[index % 8]] += 1
    execution = {}
    if gpu:
        execution = common.read_json(directory / "gpu_execution.json")
        for field, expected in (("trace_mode", "summary"), ("mapping", "packed"),
                                ("lanes_per_block", lanes), ("chunk_steps", chunk)):
            common.require_equal(f"GPU {directory.name} {field}", execution[field], expected)
        common.require_equal("GPU chunk count", execution["chunk_count"],
                             math.ceil(common.STEPS / chunk))
    return {"exit_counts": dict(reasons), "fault_profile_instances": dict(fault_profiles),
            "gpu_execution": execution}


def exact_variant_projection(root: Path, variant: str) -> str:
    primary = root / "gpu/primary"
    other = root / "gpu" / variant
    digest = common.hashlib.sha256()
    for index in range(COUNT):
        left = primary / f"instance_{index:04d}"
        right = other / f"instance_{index:04d}"
        common.require_equal(f"{variant} manifest instance {index}",
                             common.normalized_manifest(left / "run_manifest.json"),
                             common.normalized_manifest(right / "run_manifest.json"))
        for filename in ("summary.json", "event_log.txt"):
            first = common.sha256_file(left / filename)
            common.require_equal(f"{variant} instance {index} {filename}",
                                 common.sha256_file(right / filename), first)
            digest.update(f"{index}:{filename}:{first}\n".encode())
    return digest.hexdigest()


def validate_results(repo: Path, root: Path, input_info: dict,
                     write_logs: bool) -> dict:
    cpu = root / "cpu/heterogeneous_n128"
    runs = {name: root / "gpu" / name for name in GPU_VARIANTS}
    result = {"cpu": validate_output(cpu, False), "gpu": {},
              "comparisons": {}, "final_projections": {}, "exact_variant_sha256": {}}
    cpu_projection = common.cpu_final_projection(cpu, COUNT)
    result["final_projections"]["cpu_sha256"] = common.final_projection_digest(cpu_projection)
    for name, (mapping, lanes, chunk) in GPU_VARIANTS.items():
        directory = runs[name]
        result["gpu"][name] = validate_output(directory, True, chunk, lanes)
        label = f"CPU/GPU {name}"
        log = root / f"logs/compare_cpu_gpu_{name}.log" if write_logs else None
        output = common.run_comparator(repo, cpu, directory, log, label)
        result["comparisons"][name] = common.sha256_bytes(output.encode())
        gpu_projection = common.gpu_final_projection(root / f"logs/gpu_{name}.log", COUNT)
        result["final_projections"][name] = common.final_projection_digest(gpu_projection)
        for index, (c, g) in enumerate(zip(cpu_projection, gpu_projection)):
            for field, expected in (("steps", c["steps"]),
                                    ("exit_reason", c["exit_reason"]),
                                    ("status", 0), ("hit", int(c["exit_reason"] == "hit")),
                                    ("trace_records", 0)):
                common.require_equal(f"{name} instance {index} {field}", g[field], expected)
            for field in ("final_time", "final_x", "final_y"):
                if not math.isclose(c[field], g[field],
                                    rel_tol=common.RTOL, abs_tol=common.ATOL):
                    common.fail(f"{name} instance {index} {field} outside default tolerance: "
                                f"CPU={c[field]!r} GPU={g[field]!r}")
    primary_projection = common.gpu_final_projection(root / "logs/gpu_primary.log", COUNT)
    for name in ("repeat", "packed32", "chunk64"):
        variant_projection = common.gpu_final_projection(root / f"logs/gpu_{name}.log", COUNT)
        common.require_equal(f"{name} exact terminal projection", variant_projection,
                             primary_projection)
        result["exact_variant_sha256"][name] = exact_variant_projection(root, name)
    result["termination"] = {
        "counts": result["cpu"]["exit_counts"],
        "sequence": [{"instance_id": index,
                      "steps": common.read_json(
                          cpu / f"instance_{index:04d}/summary.json")["simulation_steps"],
                      "exit_reason": common.read_json(
                          cpu / f"instance_{index:04d}/summary.json")["exit_reason"]}
                     for index in range(COUNT)],
    }
    result["calibration_applied_instances"] = input_info["calibration_matches"]
    result["internal_rng_state_observed"] = False
    return result


def run(repo: Path, workspace: Path, root: Path, tracked: Path,
        skip_build: bool) -> str:
    if root.exists() or tracked.exists():
        common.fail("refusing to overwrite existing M8-A3 output or receipt")
    common.validate_repository(repo, workspace)
    input_info = validate_input(repo, workspace)
    if shutil.disk_usage(workspace).free < common.MINIMUM_FREE_BYTES:
        common.fail("insufficient disk free space for M8-A3 CPU full output")
    hardware = common.query_hardware()
    root.mkdir(parents=True)
    for name in ("logs", "runtimes", "cpu", "gpu"):
        (root / name).mkdir()
    common.run_logged([sys.executable, str(workspace / "tools/prepare_m8_cases.py"),
                       "--check"], repo, root / "logs/m8_a1_check.log", "check frozen inputs")
    if not skip_build:
        common.run_logged([str(workspace / "tools/build_cpu_baseline.sh")], repo,
                          root / "logs/build_cpu.log", "build CPU baseline")
        common.run_logged(["make", "-C", str(workspace), "-j2"], repo,
                          root / "logs/build_gpu.log", "build GPU campaign")
    binary_hashes = common.binary_evidence(workspace)
    runtime = copy.deepcopy(input_info["runtime"])
    runtime["logging"]["output_dir"] = str((root / "cpu/heterogeneous_n128").resolve())
    derived = root / "runtimes/heterogeneous_n128.json"
    common.write_json_exclusive(derived, runtime)
    monitoring = {"method": "A2 100 ms host process-tree RSS and 250 ms device-wide WDDM GPU memory sampling; diagnostic, not benchmark",
                  "disk_free_before_bytes": shutil.disk_usage(workspace).free, "runs": {}}
    cpu_binary = workspace / "build/cpu_baseline/instance_manager"
    gpu_binary = workspace / "build/bin/gpu_campaign"
    monitoring["runs"]["cpu"] = common.run_monitored(
        [str(cpu_binary), "--runtime", str(derived)], repo,
        root / "logs/cpu.log", "CPU heterogeneous N=128", False)
    for name, (mapping, lanes, chunk) in GPU_VARIANTS.items():
        monitoring["runs"][name] = common.run_monitored(
            [str(gpu_binary), "--config", str(derived), "--output-dir",
             str(root / "gpu" / name), "--trace-mode", "summary",
             "--chunk-steps", str(chunk), "--mapping", mapping,
             "--lanes-per-block", str(lanes)], repo,
            root / f"logs/gpu_{name}.log",
            f"GPU heterogeneous {name} N=128 chunk={chunk} lanes={lanes}", True)
    monitoring["disk_free_after_bytes"] = shutil.disk_usage(workspace).free
    common.write_json_exclusive(root / "monitoring.json", monitoring)
    result = validate_results(repo, root, input_info, True)
    runtime_versions = {v["gpu_execution"]["cuda_runtime_version"]
                        for v in result["gpu"].values()}
    common.require_equal("CUDA version count", len(runtime_versions), 1)
    hardware["cuda_runtime_version"] = next(iter(runtime_versions))
    receipt = {
        "schema_version": 1, "phase": "M8", "segment": "M8-A3", "status": "PASS",
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "complete_m8_gate": False, "next_segment": "M8-B1",
        "contract": {"instances": COUNT, "steps": common.STEPS, "dt_s": common.DT_S,
                     "profiles": input_info["profiles"], "cpu_trace_mode": "full",
                     "gpu_trace_mode": "summary", "gpu_variants": GPU_VARIANTS,
                     "default_rtol": common.RTOL, "default_atol": common.ATOL,
                     "a2_calibration_applies": False,
                     "direct_internal_rng_state_observed": False,
                     "full_trace_equivalence_claimed": False},
        "source": {"parent_commit": common.git(repo, ["rev-parse", "HEAD"], "parent commit"),
                   "aircraft_simu_commit": common.AIRCRAFT_COMMIT,
                   "m7_closure_sha256": common.M7_REPORT_SHA256,
                   "m8_a1_receipt_sha256": common.M8_A1_RECEIPT_SHA256,
                   "m8_a2_receipt_sha256": common.sha256_file(
                       workspace / "validation/m8_receipts/m8_a2_uniform.json"),
                   "heterogeneous_input_sha256": input_info["input_sha256"],
                   "heterogeneous_scenario_tree": input_info["scenario_tree"],
                   "runner_sha256": common.sha256_file(Path(__file__)),
                   "comparator_sha256": common.sha256_file(repo / "compare_runs.py"),
                   "gpu_source_tree": common.tree_fingerprint(repo, [
                       workspace / "gpu_campaign", workspace / "staging"]),
                   "binary_sha256": binary_hashes},
        "hardware": hardware, "monitoring": monitoring, "results": result,
        "artifacts": {"derived_inputs": common.tree_fingerprint(root, [root / "runtimes"]),
                      "results": common.tree_fingerprint(root, [root / "cpu", root / "gpu"]),
                      "logs": common.tree_fingerprint(root, [root / "logs"]),
                      "monitoring_sha256": common.sha256_file(root / "monitoring.json")},
        "validation": {"cpu_gpu_semantic": "PASS", "gpu_repeat": "PASS",
                       "mapping_and_chunk_exact": "PASS", "terminal_projection": "PASS",
                       "instance_isolation": "PASS (per-instance outputs invariant across packed/4, packed/32, and chunk=64)",
                       "performance_claimed": False},
    }
    payload = common.json_bytes(receipt)
    local = root / "m8_a3_pass_receipt.json"
    common.write_bytes_exclusive(local, payload)
    common.require_equal("local receipt", local.read_bytes(), payload)
    common.write_bytes_exclusive(tracked, payload)
    print(f"m8_a3: PASS receipt_sha256={common.sha256_bytes(payload)}", flush=True)
    return common.sha256_bytes(payload)


def check(repo: Path, workspace: Path, root: Path, tracked: Path) -> str:
    common.validate_repository(repo, workspace)
    input_info = validate_input(repo, workspace)
    receipt_bytes = tracked.read_bytes()
    common.require_equal("local/tracked receipt", (root / "m8_a3_pass_receipt.json").read_bytes(), receipt_bytes)
    receipt = common.read_json(tracked)
    common.require_equal("receipt status", receipt["status"], "PASS")
    common.require_equal("input SHA", input_info["input_sha256"],
                         receipt["source"]["heterogeneous_input_sha256"])
    common.require_equal("result files", common.tree_fingerprint(root, [
        root / "cpu", root / "gpu"]), receipt["artifacts"]["results"])
    common.require_equal("log files", common.tree_fingerprint(root, [
        root / "logs"]), receipt["artifacts"]["logs"])
    common.require_equal("result validation", validate_results(repo, root, input_info, False),
                         receipt["results"])
    return common.sha256_bytes(receipt_bytes)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("gpu_workspace/build/m8_a3_heterogeneous"))
    parser.add_argument("--receipt", type=Path, default=Path("gpu_workspace/validation/m8_receipts/m8_a3_heterogeneous.json"))
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[1]
    repo = workspace.parent
    root = args.output.resolve()
    tracked = args.receipt.resolve()
    try:
        root.relative_to((workspace / "build").resolve())
    except ValueError:
        common.fail("output must be below gpu_workspace/build")
    if root == (workspace / "build").resolve() or not root.name.startswith("m8_a3_"):
        common.fail("output must be a fresh m8_a3_* directory below gpu_workspace/build")
    try:
        tracked.relative_to((workspace / "validation/m8_receipts").resolve())
    except ValueError:
        common.fail("receipt must be below validation/m8_receipts")
    if tracked.suffix != ".json":
        common.fail("receipt must be JSON below validation/m8_receipts")
    if args.check:
        digest = check(repo, workspace, root, tracked)
    else:
        digest = run(repo, workspace, root, tracked, args.skip_build)
    print(f"m8_a3: receipt={tracked} sha256={digest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (common.M8A2Failure, OSError, KeyError, ValueError) as exc:
        print(f"m8_a3: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
