#!/usr/bin/env python3
"""Frozen M8 benchmark harness. B1 validates/freezes; B2/B3 execute later.

The production kernel CUDA-event sum and complete external process wall time
are distinct metrics. Existing evidence is never replaced. No benchmark is
run by --self-test, --check-contract or --freeze-contract.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
import platform
import signal
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence


WORKSPACE = Path(__file__).resolve().parents[1]
REPO = WORKSPACE.parent
CONTRACT = WORKSPACE / "configs/m8/benchmark_contract.json"
B1_RECEIPT = WORKSPACE / "validation/m8_receipts/m8_b1_contract.json"
B1_LOCAL = WORKSPACE / "build/m8_b1_contract/m8_b1_receipt.json"
RECEIPT_DIR = WORKSPACE / "validation/m8_receipts"
SOURCE_RECEIPTS = {
    "m8_a1_receipt_sha256": "m8_a1_inputs.json",
    "m8_a2_receipt_sha256": "m8_a2_uniform.json",
    "m8_a3_receipt_sha256": "m8_a3_heterogeneous.json",
}
BINARIES = {
    "cpu_environment": WORKSPACE / "build/cpu_baseline/environment_sim",
    "cpu_flight_control": WORKSPACE / "build/cpu_baseline/flight_control_sim",
    "cpu_instance_manager": WORKSPACE / "build/cpu_baseline/instance_manager",
    "gpu_campaign": WORKSPACE / "build/bin/gpu_campaign",
}
GPU_STAGE_FIELDS = ("h2d_ms", "kernel_ms", "d2h_ms", "host_encode_ms",
                    "output_ms", "wall_ms")


class BenchmarkError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise BenchmarkError(message)


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        fail(f"cannot read {path}: {exc}")
    if not isinstance(value, dict):
        fail(f"JSON object required: {path}")
    return value


def encoded(value: Any) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=True) + "\n").encode()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as exc:
        fail(f"cannot hash {path}: {exc}")
    return digest.hexdigest()


def source_tree_sha256() -> str:
    digest = hashlib.sha256()
    paths = [WORKSPACE / "Makefile"]
    for root in (WORKSPACE / "gpu_campaign", WORKSPACE / "staging",
                 WORKSPACE / "converted"):
        paths.extend(path for path in root.rglob("*") if path.is_file())
    for path in sorted(paths, key=lambda value: value.as_posix()):
        digest.update(path.relative_to(WORKSPACE).as_posix().encode() + b"\0" +
                      sha256(path).encode() + b"\n")
    return digest.hexdigest()


def write_exclusive(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("xb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError:
        fail(f"refusing to replace existing evidence: {path}")


def command_output(argv: Sequence[str], timeout: int = 15) -> str:
    try:
        result = subprocess.run(list(argv), cwd=REPO, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        fail(f"command failed: {argv!r}: {exc}")
    if result.returncode:
        fail(f"command failed: {argv!r}: {result.stderr.strip()}")
    return result.stdout.strip()


def validate_contract() -> dict[str, Any]:
    contract = read_json(CONTRACT)
    for key, expected in (("schema_version", 1), ("phase", "M8"),
                          ("segment", "M8-B1")):
        if contract.get(key) != expected:
            fail(f"contract {key} mismatch")
    workload = contract["workload"]
    if (workload["dataset"] != "uniform" or
        workload["instance_counts"] != [64, 128] or
        workload["steps"] != 12000 or workload["dt_s"] != 0.01 or
        workload["cpu_parallel_processes"] != 6 or
        workload["gpu_mapping"] != "packed" or
        workload["gpu_lanes_per_block"] != 4 or
        workload["gpu_chunk_steps"] != 256):
        fail("workload differs from frozen M8-A1/A2 baseline")
    expected_modes = {
        "cpu_sequential": ("cpu", "external_wall_s", 1, 5, "full"),
        "cpu_process_parallel": ("cpu", "external_wall_s", 1, 5, "full"),
        "gpu_compute_only": ("gpu", "production_kernel_event_ms", 3, 20, "summary"),
        "gpu_full": ("gpu", "external_wall_s", 1, 5, "full"),
        "gpu_summary": ("gpu", "external_wall_s", 1, 5, "summary"),
    }
    if set(contract["modes"]) != set(expected_modes):
        fail("benchmark mode set differs from frozen contract")
    if contract["mode_order"] != list(expected_modes):
        fail("mode ordering differs from frozen contract")
    for mode, expected in expected_modes.items():
        item = contract["modes"][mode]
        actual = tuple(item[key] for key in
                       ("section", "metric", "warmup", "repeats", "trace_mode"))
        if actual != expected:
            fail(f"benchmark mode {mode} differs from frozen contract")
    if contract["timing_semantics"]["stage_fields_are_additive"] is not False:
        fail("GPU stages must be marked non-additive")
    for count in workload["instance_counts"]:
        runtime = REPO / workload["runtimes"][str(count)]
        value = read_json(runtime)
        if value["campaign"]["instance_count"] != count or \
           value["campaign"]["schedule"] != "PARALLEL" or \
           value["campaign"]["max_parallel_instances"] != 6:
            fail(f"frozen runtime N={count} is invalid")
    return contract


def summarize(samples: Sequence[float]) -> dict[str, Any]:
    if not samples or any(not math.isfinite(value) or value < 0
                          for value in samples):
        fail("samples must be non-empty, finite and non-negative")
    ordered = sorted(samples)
    rank = 0.95 * (len(ordered) - 1)
    low = math.floor(rank)
    high = math.ceil(rank)
    p95 = ordered[low] + (ordered[high] - ordered[low]) * (rank - low)
    return {"n": len(ordered), "median_s": statistics.median(ordered),
            "p95_s": p95, "min_s": ordered[0], "max_s": ordered[-1]}


def make_plan(contract: dict[str, Any], section: str) -> list[dict[str, Any]]:
    if section not in ("cpu", "gpu"):
        fail(f"invalid section: {section}")
    modes = [(name, contract["modes"][name]) for name in contract["mode_order"]
             if contract["modes"][name]["section"] == section]
    plan: list[dict[str, Any]] = []
    for count in contract["workload"]["instance_counts"]:
        for phase in ("warmup", "repeats"):
            for ordinal in range(max(value[phase] for _, value in modes)):
                for mode, value in modes:
                    if ordinal < value[phase]:
                        plan.append({"count": count, "mode": mode,
                                     "phase": "measure" if phase == "repeats" else phase,
                                     "ordinal": ordinal})
    return plan


def self_test() -> list[str]:
    names: list[str] = []
    def check(name: str, condition: bool) -> None:
        if not condition:
            fail(f"self-test {name} failed")
        names.append(name)
    contract = validate_contract()
    values = summarize([float(i) for i in range(1, 21)])
    check("even median", values["median_s"] == 10.5)
    check("20-sample interpolated P95", values["p95_s"] == 19.05)
    check("20-sample extrema", values["min_s"] == 1.0 and values["max_s"] == 20.0)
    check("5-sample interpolated P95", summarize([1, 2, 3, 4, 5])["p95_s"] == 4.8)
    for bad in ([], [-1.0], [math.nan], [math.inf]):
        try:
            summarize(bad)
        except BenchmarkError:
            continue
        fail(f"self-test accepted bad samples: {bad!r}")
    names.append("invalid sample rejection")
    cpu = make_plan(contract, "cpu")
    gpu = make_plan(contract, "gpu")
    check("CPU 1+5 x 2 modes x 2 counts", len(cpu) == 24)
    check("GPU (3+20)+(1+5)x2 x 2 counts", len(gpu) == 70)
    check("CPU warmup precedes measures", cpu[0]["phase"] == "warmup" and
          cpu[2]["phase"] == "measure")
    check("GPU compute has 20 measured samples each count",
          sum(item["mode"] == "gpu_compute_only" and item["phase"] == "measure"
              for item in gpu) == 40)
    try:
        checked_run_root(REPO, "cpu")
    except BenchmarkError:
        names.append("broad output root rejected")
    else:
        fail("self-test accepted repository root as benchmark output")
    try:
        checked_run_receipt(REPO / "unexpected.json", "gpu")
    except BenchmarkError:
        names.append("out-of-scope receipt rejected")
    else:
        fail("self-test accepted out-of-scope benchmark receipt")
    return names


def get_provenance(contract: dict[str, Any]) -> dict[str, Any]:
    parent = command_output(["git", "rev-parse", "HEAD"])
    aircraft = command_output(["git", "-C", str(REPO / "external/Aircraft-Simu"),
                               "rev-parse", "HEAD"])
    if aircraft != "970bdfbe97db21aafb6cd94afee0ecbe72c8544c":
        fail("Aircraft-Simu baseline commit changed")
    if command_output(["git", "-C", str(REPO / "external/Aircraft-Simu"),
                       "status", "--porcelain", "--untracked-files=no"]):
        fail("Aircraft-Simu tracked worktree is not clean")
    gpu_row = command_output(["nvidia-smi", "--query-gpu=name,driver_version,memory.total",
                              "--format=csv,noheader,nounits"])
    fields = [item.strip() for item in gpu_row.splitlines()[0].split(",")]
    if len(fields) != 3:
        fail(f"unexpected GPU hardware row: {gpu_row!r}")
    a3 = read_json(RECEIPT_DIR / "m8_a3_heterogeneous.json")
    gpu_binary_sha = sha256(BINARIES["gpu_campaign"])
    if gpu_binary_sha != a3["source"]["binary_sha256"]["gpu_campaign"]:
        fail("GPU executable differs from the protected M8-A3 correctness binary")
    cpu_model = "unknown"
    for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
        if line.startswith("model name"):
            cpu_model = line.partition(":")[2].strip()
            break
    return {
        "parent_commit": parent,
        "aircraft_simu_commit": aircraft,
        "m7_report_sha256": sha256(WORKSPACE / "validation/correctness_m7.json"),
        **{key: sha256(RECEIPT_DIR / filename)
           for key, filename in SOURCE_RECEIPTS.items()},
        "contract_sha256": sha256(CONTRACT),
        "harness_sha256": sha256(Path(__file__)),
        "input_sha256": {str(count): sha256(REPO / contract["workload"]["runtimes"][str(count)])
                         for count in contract["workload"]["instance_counts"]},
        "binary_sha256": {name: sha256(path) for name, path in BINARIES.items()},
        "gpu_source_tree_sha256": source_tree_sha256(),
        "os": platform.platform(), "cpu_model": cpu_model,
        "cpu_affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
        "gpu_model": fields[0], "gpu_driver": fields[1],
        "gpu_memory_mib": int(fields[2]),
        "cuda_runtime_version": a3["hardware"]["cuda_runtime_version"],
        "cuda_toolkit": command_output(["/usr/local/cuda-13.2/bin/nvcc", "--version"]),
        "gcc_version": command_output(["gcc", "--version"]).splitlines()[0],
        "cuda_visible_devices": os.environ.get("CUDA_VISIBLE_DEVICES"),
    }


def validate_prior_receipts(provenance: dict[str, Any]) -> None:
    expected = {
        "m8_a1_receipt_sha256": "c8273dc5e3211d0d6a8560a419ebf217e0ec7bd47a703c56dd78eed0fe1778eb",
        "m8_a2_receipt_sha256": "73674f9df5378792c7659b4a6436ea9be61df1c383282a2e6f6209fd34c8354d",
        "m8_a3_receipt_sha256": "e0c77fc674444461857e7a56a9c821c62333dabd37aae00218ae82d51405898d",
        "m7_report_sha256": "6068d5bb44c3fdc4bde11ea05aabfc7cffbde8a165db1f88bce4450ac2b90627",
    }
    for key, digest in expected.items():
        if provenance[key] != digest:
            fail(f"protected prerequisite {key} changed")


def freeze_contract() -> str:
    if B1_LOCAL.parent.exists() or B1_RECEIPT.exists():
        fail("M8-B1 output or receipt already exists")
    tests = self_test()
    contract = validate_contract()
    provenance = get_provenance(contract)
    validate_prior_receipts(provenance)
    plan = {section: make_plan(contract, section) for section in ("cpu", "gpu")}
    receipt = {
        "schema_version": 1, "phase": "M8", "segment": "M8-B1",
        "status": "PASS", "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "complete_m8_gate": False, "next_segment": "M8-B2",
        "source": provenance,
        "contract": contract,
        "dry_run_plan": plan,
        "self_tests": tests,
        "validation": {"schema": "PASS", "statistics": "PASS",
                       "run_order": "PASS", "predecessor_receipts": "PASS",
                       "real_benchmark_campaigns_executed": False,
                       "performance_claimed": False},
    }
    payload = encoded(receipt)
    write_exclusive(B1_LOCAL, payload)
    if B1_LOCAL.read_bytes() != payload:
        fail("local receipt read-back mismatch")
    write_exclusive(B1_RECEIPT, payload)
    return hashlib.sha256(payload).hexdigest()


def check_contract() -> str:
    tests = self_test()
    contract = validate_contract()
    local = B1_LOCAL.read_bytes()
    tracked = B1_RECEIPT.read_bytes()
    if local != tracked:
        fail("M8-B1 local/tracked receipts differ")
    receipt = read_json(B1_RECEIPT)
    if receipt["status"] != "PASS" or receipt["contract"] != contract or \
       receipt["self_tests"] != tests:
        fail("M8-B1 receipt contract or tests changed")
    if receipt["source"]["contract_sha256"] != sha256(CONTRACT) or \
       receipt["source"]["harness_sha256"] != sha256(Path(__file__)):
        fail("M8-B1 harness or contract source hash changed")
    if receipt["dry_run_plan"] != {section: make_plan(contract, section)
                                   for section in ("cpu", "gpu")}:
        fail("M8-B1 command plan changed")
    for key, filename in SOURCE_RECEIPTS.items():
        if receipt["source"][key] != sha256(RECEIPT_DIR / filename):
            fail(f"protected receipt {key} changed")
    return hashlib.sha256(tracked).hexdigest()


def checked_run_root(root: Path, section: str) -> Path:
    resolved = root.resolve()
    build = (WORKSPACE / "build").resolve()
    try:
        resolved.relative_to(build)
    except ValueError:
        fail("benchmark output must be below gpu_workspace/build")
    expected = "m8_b2_" if section == "cpu" else "m8_b3_"
    if resolved == build or not resolved.name.startswith(expected):
        fail(f"benchmark output must be a fresh {expected}* directory")
    return resolved


def checked_run_receipt(path: Path, section: str) -> Path:
    resolved = path.resolve()
    try:
        resolved.relative_to(RECEIPT_DIR.resolve())
    except ValueError:
        fail("benchmark receipt must be below validation/m8_receipts")
    expected = "m8_b2_" if section == "cpu" else "m8_b3_"
    if not resolved.name.startswith(expected) or resolved.suffix != ".json":
        fail(f"benchmark receipt must be named {expected}*.json")
    return resolved


def verify_section_gate(section: str) -> None:
    progress = read_json(WORKSPACE / "validation/performance_m8_progress.json")
    expected = "M8-B2" if section == "cpu" else "M8-B3"
    if progress["current_segment"]["id"] != expected or \
       progress["current_segment"]["status"] != "NEXT":
        fail(f"{expected} is not the active NEXT segment")


def validate_binary_freeze(provenance: dict[str, Any]) -> None:
    for name, path in BINARIES.items():
        if sha256(path) != provenance["binary_sha256"][name]:
            fail(f"{name} differs from M8-B1 frozen binary; rebaseline before timing")
    if source_tree_sha256() != provenance["gpu_source_tree_sha256"]:
        fail("GPU source tree differs from M8-B1 frozen source")


def output_semantics(directory: Path, count: int, gpu: bool) -> dict[str, Any]:
    campaign = read_json(directory / "campaign_summary.json")
    for field, expected in (("instance_count", count), ("completed_count", count),
                            ("failed_count", 0), ("summary_available_count", count)):
        if campaign[field] != expected:
            fail(f"{directory}: {field}={campaign[field]!r}, expected {expected!r}")
    if len(campaign["instances"]) != count:
        fail(f"{directory}: wrong campaign instance count")
    digest = hashlib.sha256()
    for index in range(count):
        instance = directory / f"instance_{index:04d}"
        summary = read_json(instance / "summary.json")
        manifest = read_json(instance / "run_manifest.json")
        if manifest["instance_id"] != index or \
           manifest["random_seed"] != 12345 + 17 * index or \
           summary["exit_reason"] not in ("timeout", "surface_collision") or \
           not 0 < summary["simulation_steps"] <= 12000:
            fail(f"{directory}: invalid instance {index} terminal semantics")
        if summary["exit_reason"] == "timeout" and summary["simulation_steps"] != 12000:
            fail(f"{directory}: instance {index} timed out early")
        event_hash = sha256(instance / "event_log.txt")
        digest.update(str(index).encode() + b"\0" +
                      json.dumps(summary, sort_keys=True, separators=(",", ":")).encode() +
                      b"\0" + event_hash.encode() + b"\n")
    result: dict[str, Any] = {"semantic_sha256": digest.hexdigest(),
                              "campaign_summary_sha256": sha256(directory / "campaign_summary.json")}
    if gpu:
        metadata = read_json(directory / "gpu_execution.json")
        for key in GPU_STAGE_FIELDS:
            value = metadata.get(key)
            if not isinstance(value, (int, float)) or \
               not math.isfinite(value) or value < 0:
                fail(f"{directory}: invalid GPU timing field {key}={value!r}")
        if metadata["kernel_ms"] <= 0:
            fail(f"{directory}: non-positive production kernel event time")
        result["gpu_execution"] = metadata
        result["gpu_execution_sha256"] = sha256(directory / "gpu_execution.json")
    return result


def validate_mode_output(directory: Path, mode: str, semantics: dict[str, Any],
                         contract: dict[str, Any]) -> None:
    if mode.startswith("cpu_"):
        campaign = read_json(directory / "campaign_summary.json")
        expected = "SEQUENTIAL" if mode == "cpu_sequential" else "PARALLEL"
        if campaign["schedule"] != expected:
            fail(f"{directory}: unexpected CPU schedule")
        return
    metadata = semantics["gpu_execution"]
    expected = {"trace_mode": contract["modes"][mode]["trace_mode"],
                "mapping": contract["workload"]["gpu_mapping"],
                "lanes_per_block": contract["workload"]["gpu_lanes_per_block"],
                "chunk_steps": contract["workload"]["gpu_chunk_steps"]}
    for key, value in expected.items():
        if metadata.get(key) != value:
            fail(f"{directory}: GPU {key} differs from frozen contract")
    if metadata.get("cuda_runtime_version") != \
       read_json(B1_RECEIPT)["source"]["cuda_runtime_version"]:
        fail(f"{directory}: CUDA runtime differs from M8-B1 freeze")


def compare_shared_semantics(cpu: Path, gpu: Path, count: int) -> None:
    """Compare CPU and GPU shared summaries/events even if GPU emitted full traces."""
    sys.path.insert(0, str(REPO))
    import compare_runs  # pylint: disable=import-outside-toplevel
    left_campaign = read_json(cpu / "campaign_summary.json")
    right_campaign = read_json(gpu / "campaign_summary.json")
    diffs = compare_runs.compare_json_fields(
        left_campaign, right_campaign, "campaign_summary",
        fields=compare_runs.CAMPAIGN_FIELDS, rtol=1e-10, atol=1e-12)
    for index in range(count):
        matched, instance_diffs = compare_runs.compare_instance(
            cpu, gpu, index, 1e-10, 1e-12, False)
        if not matched:
            diffs.extend(instance_diffs[:5])
        if len(diffs) >= 10:
            break
    if diffs:
        fail(f"CPU/GPU shared semantic comparison failed for {gpu}:\n" +
             "\n".join(diffs[:10]))


def run_process(argv: Sequence[str], log_path: Path) -> float:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter_ns()
    with log_path.open("xb") as log:
        process = subprocess.Popen(list(argv), cwd=REPO, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=3600)
        except subprocess.TimeoutExpired:
            # Only the process group created for this benchmark command.
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            fail(f"benchmark command timed out after 3600 s: {argv!r}")
    seconds = (time.perf_counter_ns() - started) / 1e9
    if code:
        tail = log_path.read_text(encoding="utf-8", errors="replace").splitlines()[-25:]
        fail(f"benchmark command exited {code}: {argv!r}\n" + "\n".join(tail))
    return seconds


def derive_command(contract: dict[str, Any], item: dict[str, Any],
                   root: Path) -> tuple[list[str], Path, Path | None]:
    count = item["count"]
    mode = item["mode"]
    slug = f"{item['phase']}_{item['ordinal']:02d}"
    output = root / "runs" / f"n{count}" / mode / slug
    if output.exists():
        fail(f"refusing to replace existing benchmark result: {output}")
    source = REPO / contract["workload"]["runtimes"][str(count)]
    runtime_path: Path | None = None
    if mode.startswith("cpu_"):
        runtime = copy.deepcopy(read_json(source))
        runtime["logging"]["output_dir"] = str(output)
        if mode == "cpu_sequential":
            runtime["campaign"]["schedule"] = "SEQUENTIAL"
            runtime["campaign"]["max_parallel_instances"] = 1
        else:
            runtime["campaign"]["schedule"] = "PARALLEL"
            runtime["campaign"]["max_parallel_instances"] = 6
        runtime_path = root / "runtimes" / f"n{count}" / mode / f"{slug}.json"
        write_exclusive(runtime_path, encoded(runtime))
        argv = [str(BINARIES["cpu_instance_manager"]), "--runtime", str(runtime_path)]
    else:
        argv = [str(BINARIES["gpu_campaign"]), "--config", str(source),
                "--output-dir", str(output), "--trace-mode",
                contract["modes"][mode]["trace_mode"], "--mapping", "packed",
                "--lanes-per-block", "4", "--chunk-steps", "256"]
    return argv, output, runtime_path


def group_measurements(contract: dict[str, Any], section: str,
                       records: Sequence[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    grouped: dict[str, dict[str, Any]] = {}
    for count in contract["workload"]["instance_counts"]:
        for mode, config in contract["modes"].items():
            if config["section"] != section:
                continue
            measured = [entry for entry in records if entry["count"] == count and
                        entry["mode"] == mode and entry["phase"] == "measure"]
            warmups = [entry for entry in records if entry["count"] == count and
                       entry["mode"] == mode and entry["phase"] == "warmup"]
            if len(measured) != config["repeats"] or len(warmups) != config["warmup"]:
                fail(f"missing samples for N={count} {mode}")
            if len({entry["semantics"]["semantic_sha256"] for entry in measured + warmups}) != 1:
                fail(f"non-deterministic per-instance semantics for N={count} {mode}")
            grouped[f"n{count}_{mode}"] = {
                "metric": config["metric"], "warmup_count": len(warmups),
                "statistics": summarize([entry["sample_s"] for entry in measured]),
                "samples_s": [entry["sample_s"] for entry in measured],
                "semantic_sha256": measured[0]["semantics"]["semantic_sha256"],
            }
        digests = {value["semantic_sha256"] for key, value in grouped.items()
                   if key.startswith(f"n{count}_")}
        if len(digests) != 1:
            fail(f"N={count} {section} modes produced different per-instance results")
    return grouped


def run_section(section: str, root: Path, tracked: Path, resume: bool,
                cpu_reference: Path | None) -> str:
    verify_section_gate(section)
    frozen_hash = check_contract()
    contract = validate_contract()
    frozen = read_json(B1_RECEIPT)
    validate_binary_freeze(frozen["source"])
    provenance = get_provenance(contract)
    validate_prior_receipts(provenance)
    if (provenance["gpu_model"] != frozen["source"]["gpu_model"] or
        provenance["gpu_driver"] != frozen["source"]["gpu_driver"] or
        provenance["cuda_toolkit"] != frozen["source"]["cuda_toolkit"]):
        fail("GPU/driver/CUDA toolchain differs from M8-B1 freeze")
    if section == "gpu":
        if cpu_reference is None:
            fail("GPU benchmark requires --cpu-reference from completed M8-B2")
        cpu_receipt = read_json(cpu_reference / "benchmark_receipt.json")
        if cpu_receipt.get("segment") != "M8-B2" or \
           cpu_receipt.get("status") != "MEASURED_NO_VERDICT" or \
           cpu_receipt.get("m8_b1_receipt_sha256") != frozen_hash:
            fail("CPU reference is not a completed M8-B2 run of this frozen protocol")
        for count in contract["workload"]["instance_counts"]:
            reference = cpu_reference / "runs" / f"n{count}" / \
                "cpu_process_parallel/measure_00"
            output_semantics(reference, count, False)
    elif cpu_reference is not None:
        fail("--cpu-reference applies only to GPU benchmark section")
    plan = make_plan(contract, section)
    manifest_path = root / "benchmark_manifest.json"
    manifest = {"schema_version": 1, "section": section, "source": provenance,
                "m8_b1_receipt_sha256": frozen_hash,
                "contract_sha256": sha256(CONTRACT), "command_order": plan,
                "cpu_reference": str(cpu_reference) if cpu_reference else None}
    if tracked.exists():
        fail(f"refusing to replace existing benchmark receipt: {tracked}")
    if resume:
        if not root.is_dir() or not manifest_path.is_file():
            fail("--resume requires an initialized benchmark output and manifest")
        if read_json(manifest_path) != manifest:
            fail("resume manifest differs from the current binaries, inputs, environment or plan")
    else:
        if root.exists():
            fail(f"refusing to replace existing benchmark output: {root}")
        root.mkdir(parents=True)
        write_exclusive(manifest_path, encoded(manifest))
    records: list[dict[str, Any]] = []
    for position, item in enumerate(plan):
        record_path = root / "records" / f"{position:03d}.json"
        if resume and record_path.is_file():
            previous = read_json(record_path)
            if any(previous.get(key) != value for key, value in item.items()):
                fail(f"resume record {position} does not match the frozen plan")
            old_output = root / previous["output"]
            old_log = root / previous["log"]
            if previous["log_sha256"] != sha256(old_log) or \
               previous["semantics"] != output_semantics(
                   old_output, item["count"], section == "gpu"):
                fail(f"resume record {position} artifacts changed")
            validate_mode_output(old_output, item["mode"], previous["semantics"], contract)
            if previous["derived_runtime"] is not None and \
               not (root / previous["derived_runtime"]).is_file():
                fail(f"resume record {position} runtime is missing")
            records.append(previous)
            print(f"m8_benchmark: verified completed run {position + 1}/{len(plan)}",
                  flush=True)
            continue
        if record_path.exists():
            fail(f"unexpected record path: {record_path}")
        argv, output, runtime = derive_command(contract, item, root)
        log = root / "logs" / f"{position:03d}_n{item['count']}_{item['mode']}_{item['phase']}_{item['ordinal']:02d}.log"
        print(f"m8_benchmark: {section} {position + 1}/{len(plan)} "
              f"N={item['count']} {item['mode']} {item['phase']} {item['ordinal']}", flush=True)
        external_wall = run_process(argv, log)
        semantics = output_semantics(output, item["count"], section == "gpu")
        validate_mode_output(output, item["mode"], semantics, contract)
        metric = contract["modes"][item["mode"]]["metric"]
        sample = (semantics["gpu_execution"]["kernel_ms"] / 1000.0
                  if metric == "production_kernel_event_ms" else external_wall)
        record = {**item, "command": argv, "output": str(output.relative_to(root)),
                  "log": str(log.relative_to(root)),
                  "derived_runtime": str(runtime.relative_to(root)) if runtime else None,
                  "external_wall_s": external_wall, "metric": metric,
                  "sample_s": sample, "semantics": semantics,
                  "log_sha256": sha256(log)}
        write_exclusive(record_path, encoded(record))
        records.append(record)
    if section == "gpu":
        assert cpu_reference is not None
        for record in records:
            reference = cpu_reference / "runs" / f"n{record['count']}" / \
                "cpu_process_parallel/measure_00"
            compare_shared_semantics(reference, root / record["output"], record["count"])
    grouped = group_measurements(contract, section, records)
    receipt = {
        "schema_version": 1, "phase": "M8",
        "segment": "M8-B2" if section == "cpu" else "M8-B3",
        "status": "MEASURED_NO_VERDICT",
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "m8_b1_receipt_sha256": frozen_hash,
        "source": manifest["source"], "section": section, "contract": contract,
        "cpu_reference": manifest["cpu_reference"],
        "command_order": plan, "records": records, "measurements": grouped,
        "validation": {"all_campaigns_completed": True,
                       "within_mode_semantics_exact": True,
                       "across_mode_semantics_exact": True,
                       "cpu_gpu_shared_semantics": section == "gpu",
                       "performance_threshold_claimed": False},
    }
    local_receipt = root / "benchmark_receipt.json"
    if local_receipt.exists():
        if not resume:
            fail(f"refusing to replace local receipt: {local_receipt}")
        existing = read_json(local_receipt)
        receipt["created_at_utc"] = existing.get("created_at_utc")
        if encoded(receipt) != local_receipt.read_bytes():
            fail("resumed local receipt does not match validated artifacts")
    else:
        write_exclusive(local_receipt, encoded(receipt))
    payload = local_receipt.read_bytes()
    write_exclusive(tracked, payload)
    return hashlib.sha256(payload).hexdigest()


def check_run(section: str, root: Path, tracked: Path) -> str:
    frozen_hash = check_contract()
    contract = validate_contract()
    payload = tracked.read_bytes()
    if payload != (root / "benchmark_receipt.json").read_bytes():
        fail("benchmark local/tracked receipts differ")
    receipt = read_json(tracked)
    if receipt.get("segment") != ("M8-B2" if section == "cpu" else "M8-B3") or \
       receipt.get("status") != "MEASURED_NO_VERDICT" or \
       receipt.get("m8_b1_receipt_sha256") != frozen_hash or \
       receipt.get("contract") != contract:
        fail("benchmark receipt does not match frozen section/contract")
    plan = make_plan(contract, section)
    if receipt["command_order"] != plan or len(receipt["records"]) != len(plan):
        fail("benchmark receipt command order is incomplete or changed")
    manifest = read_json(root / "benchmark_manifest.json")
    if (manifest["command_order"] != plan or manifest["section"] != section or
        manifest["source"] != receipt["source"]):
        fail("benchmark manifest differs from receipt")
    records: list[dict[str, Any]] = []
    for position, item in enumerate(plan):
        record = read_json(root / "records" / f"{position:03d}.json")
        if record != receipt["records"][position] or \
           any(record.get(key) != value for key, value in item.items()):
            fail(f"benchmark record {position} differs from receipt or plan")
        if sha256(root / record["log"]) != record["log_sha256"]:
            fail(f"benchmark log {position} changed")
        semantic = output_semantics(root / record["output"], item["count"], section == "gpu")
        if semantic != record["semantics"]:
            fail(f"benchmark output {position} changed")
        validate_mode_output(root / record["output"], item["mode"], semantic, contract)
        expected_sample = (semantic["gpu_execution"]["kernel_ms"] / 1000.0
                           if record["metric"] == "production_kernel_event_ms"
                           else record["external_wall_s"])
        if record["sample_s"] != expected_sample or \
           not math.isfinite(record["external_wall_s"]) or record["external_wall_s"] <= 0:
            fail(f"benchmark sample {position} is invalid")
        records.append(record)
    if receipt["measurements"] != group_measurements(contract, section, records):
        fail("benchmark statistics differ from recorded samples")
    if section == "gpu":
        reference_root = Path(receipt["cpu_reference"])
        for record in records:
            reference = reference_root / "runs" / f"n{record['count']}" / \
                "cpu_process_parallel/measure_00"
            compare_shared_semantics(reference, root / record["output"], record["count"])
    return hashlib.sha256(payload).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--self-test", action="store_true")
    action.add_argument("--freeze-contract", action="store_true")
    action.add_argument("--check-contract", action="store_true")
    action.add_argument("--run-section", choices=("cpu", "gpu"))
    action.add_argument("--check-run-section", choices=("cpu", "gpu"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--cpu-reference", type=Path,
                        help="M8-B2 output root required for M8-B3 CPU/GPU shared comparison")
    parser.add_argument("--resume", action="store_true",
                        help="validate completed run records and continue without replacing evidence")
    args = parser.parse_args()
    if args.self_test:
        names = self_test()
        print(f"m8_b1: PASS {len(names)} synthetic contract/statistics/plan checks")
    elif args.freeze_contract:
        print(f"m8_b1: PASS receipt_sha256={freeze_contract()}")
    elif args.check_contract:
        print(f"m8_b1: PASS checked receipt_sha256={check_contract()}")
    elif args.run_section or args.check_run_section:
        if args.output is None or args.receipt is None:
            parser.error("benchmark run/check requires --output and --receipt")
        section = args.run_section or args.check_run_section
        root = checked_run_root(args.output, section)
        tracked = checked_run_receipt(args.receipt, section)
        cpu_reference = args.cpu_reference.resolve() if args.cpu_reference else None
        digest = (run_section(section, root, tracked, args.resume, cpu_reference)
                  if args.run_section else check_run(section, root, tracked))
        print(f"m8_benchmark: section={section} receipt_sha256={digest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (BenchmarkError, OSError, KeyError, ValueError) as exc:
        print(f"m8_b1: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
