#!/usr/bin/env python3
"""Run the bounded M8-A2 uniform long CPU/GPU correctness baseline.

This segment executes the frozen M8-A1 N=64 and N=128 uniform runtimes for
up to 12000 steps.  CPU campaigns keep their normal full output; CUDA campaigns use
summary mode, chunk=256 and packed/4.  The shared semantic payload is compared
with the M7 tolerances, GPU runs are repeated, and the first 64 instance
summaries must remain byte-identical when the batch grows to 128.

The runner never removes or replaces an output or receipt.  ``--check``
revalidates existing ignored results and the tracked historical receipt without
rerunning the campaigns.  It reports when the current checker or rebuilt binary
differs from the versions recorded when the receipt was created.
"""

from __future__ import annotations

import argparse
import copy
import csv
import hashlib
import json
import math
import os
import platform
import re
import resource
import signal
import shutil
import struct
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, NoReturn, Sequence


COUNTS = (64, 128)
STEPS = 12000
DT_S = 0.01
RTOL = 1.0e-10
ATOL = 1.0e-12
CHUNK_STEPS = 256
EXPECTED_CHUNKS = 47
LANES = 4
MINIMUM_FREE_BYTES = 4 * 1024 ** 3
AIRCRAFT_COMMIT = "970bdfbe97db21aafb6cd94afee0ecbe72c8544c"
M7_REPORT_SHA256 = "6068d5bb44c3fdc4bde11ea05aabfc7cffbde8a165db1f88bce4450ac2b90627"
M8_A1_RECEIPT_SHA256 = "c8273dc5e3211d0d6a8560a419ebf217e0ec7bd47a703c56dd78eed0fe1778eb"
M8_A2_START_PARENT = "e4c229421f5d854c05b610b70075b28480d8350a"
CALIBRATION_POLICY_NAME = "m8_a2_quantization_calibration.json"
PACKET_HEADER = struct.Struct("<IHHHHIIdII")
SENSOR_PAYLOAD = struct.Struct("<I23dII")
COMMAND_PAYLOAD = struct.Struct("<I18dII")
TIMING_KEYS = frozenset({
    "campaign_wall_time_s", "total_instance_wall_time_s",
    "max_instance_wall_time_s", "wall_time_s", "h2d_ms", "kernel_ms",
    "d2h_ms", "host_encode_ms", "output_ms", "wall_ms",
})
FINAL_STATE_RE = re.compile(
    r"^gpu_campaign: instance=(?P<instance>\d+) steps=(?P<steps>\d+) "
    r"final_time=(?P<time>[-+0-9.eE]+) final_x=(?P<x>[-+0-9.eE]+) "
    r"final_y=(?P<y>[-+0-9.eE]+) exit_reason=(?P<exit>\S+) "
    r"status=(?P<status>\d+) hit=(?P<hit>\d+) "
    r"min_range=(?P<min_range>[-+0-9.eE]+) "
    r"trace_records=(?P<trace_records>\d+)$")


class M8A2Failure(RuntimeError):
    pass


def fail(message: str) -> NoReturn:
    raise M8A2Failure(message)


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError) as exc:
        fail(f"cannot read JSON {path}: {exc}")
    if not isinstance(value, dict):
        fail(f"JSON root must be an object: {path}")
    return value


def json_bytes(value: Any) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=True) + "\n").encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    if not path.is_file():
        fail(f"required file is missing: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json_exclusive(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("xb") as stream:
            stream.write(json_bytes(value))
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError:
        fail(f"refusing to replace existing file: {path}")


def write_bytes_exclusive(path: Path, value: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("xb") as stream:
            stream.write(value)
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError:
        fail(f"refusing to replace existing evidence: {path}")


def git(directory: Path, arguments: Sequence[str], label: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(directory), *arguments], text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        fail(f"{label} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def require_equal(label: str, actual: Any, expected: Any) -> None:
    if actual != expected:
        fail(f"{label} mismatch: expected {expected!r}, got {actual!r}")


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


def run_logged(command: Sequence[str], cwd: Path, log_path: Path,
               label: str) -> None:
    print(f"m8_a2: {label}", flush=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("xb") as log:
        result = subprocess.run(list(command), cwd=cwd, stdout=log,
                                stderr=subprocess.STDOUT, check=False)
    if result.returncode != 0:
        tail = log_path.read_text(encoding="utf-8", errors="replace").splitlines()[-30:]
        fail(f"{label} failed with status {result.returncode}\n" +
             "\n".join(tail))


def process_tree_memory(root_pid: int) -> dict[str, int]:
    table: dict[int, tuple[int, int, int, int, int]] = {}
    try:
        entries = list(Path("/proc").iterdir())
    except OSError:
        return {"rss_bytes": 0, "hwm_bytes": 0, "locked_bytes": 0,
                "pinned_bytes": 0, "process_count": 0}
    for entry in entries:
        if not entry.name.isdigit():
            continue
        try:
            lines = (entry / "status").read_text(
                encoding="utf-8", errors="replace").splitlines()
        except (FileNotFoundError, PermissionError, ProcessLookupError, OSError):
            continue
        parent = 0
        rss_kib = 0
        hwm_kib = 0
        locked_kib = 0
        pinned_kib = 0
        for line in lines:
            if line.startswith("PPid:"):
                parent = int(line.split()[1])
            elif line.startswith("VmRSS:"):
                rss_kib = int(line.split()[1])
            elif line.startswith("VmHWM:"):
                hwm_kib = int(line.split()[1])
            elif line.startswith("VmLck:"):
                locked_kib = int(line.split()[1])
            elif line.startswith("VmPin:"):
                pinned_kib = int(line.split()[1])
        table[int(entry.name)] = (parent, rss_kib, hwm_kib, locked_kib,
                                  pinned_kib)
    descendants = {root_pid}
    changed = True
    while changed:
        changed = False
        for pid, (parent, _, _, _, _) in table.items():
            if parent in descendants and pid not in descendants:
                descendants.add(pid)
                changed = True
    rss_kib = sum(table.get(pid, (0, 0, 0, 0, 0))[1]
                  for pid in descendants)
    hwm_kib = sum(table.get(pid, (0, 0, 0, 0, 0))[2]
                  for pid in descendants)
    locked_kib = sum(table.get(pid, (0, 0, 0, 0, 0))[3]
                     for pid in descendants)
    pinned_kib = sum(table.get(pid, (0, 0, 0, 0, 0))[4]
                     for pid in descendants)
    present = sum(1 for pid in descendants if pid in table)
    return {
        "rss_bytes": rss_kib * 1024,
        "hwm_bytes": hwm_kib * 1024,
        "locked_bytes": locked_kib * 1024,
        "pinned_bytes": pinned_kib * 1024,
        "process_count": present,
    }


def terminate_owned_process_group(process: subprocess.Popen[Any]) -> None:
    """Reap only the process group created by ``run_monitored``."""
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        if process.poll() is None:
            process.wait()
        return
    try:
        if process.poll() is None:
            process.wait(timeout=5)
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            try:
                os.killpg(process.pid, 0)
            except ProcessLookupError:
                return
            time.sleep(0.10)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    if process.poll() is None:
        process.wait()


def query_gpu_memory_mib() -> int:
    result = subprocess.run(
        ["nvidia-smi", "--query-gpu=memory.used",
         "--format=csv,noheader,nounits"], text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5,
        check=False)
    if result.returncode != 0:
        fail("nvidia-smi memory query failed: " + result.stderr.strip())
    try:
        return int(result.stdout.strip().splitlines()[0].strip())
    except (IndexError, ValueError) as exc:
        fail(f"invalid nvidia-smi memory result: {result.stdout!r} ({exc})")


def run_monitored(command: Sequence[str], cwd: Path, log_path: Path,
                  label: str, monitor_gpu: bool) -> dict[str, Any]:
    print(f"m8_a2: {label}", flush=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    baseline_gpu = query_gpu_memory_mib() if monitor_gpu else None
    peak_gpu = baseline_gpu
    peak_rss = 0
    peak_hwm = 0
    peak_locked = 0
    peak_pinned = 0
    peak_processes = 0
    rss_samples = 0
    gpu_samples = 0
    started = time.monotonic()
    next_gpu_sample = started
    with log_path.open("xb") as log:
        process = subprocess.Popen(list(command), cwd=cwd, stdout=log,
                                   stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            while True:
                memory = process_tree_memory(process.pid)
                peak_rss = max(peak_rss, memory["rss_bytes"])
                peak_hwm = max(peak_hwm, memory["hwm_bytes"])
                peak_locked = max(peak_locked, memory["locked_bytes"])
                peak_pinned = max(peak_pinned, memory["pinned_bytes"])
                peak_processes = max(peak_processes,
                                     memory["process_count"])
                rss_samples += 1
                now = time.monotonic()
                if monitor_gpu and now >= next_gpu_sample:
                    current_gpu = query_gpu_memory_mib()
                    peak_gpu = max(int(peak_gpu), current_gpu)
                    gpu_samples += 1
                    next_gpu_sample = now + 0.25
                returncode = process.poll()
                if returncode is not None:
                    break
                time.sleep(0.10)
            process.wait()
        except BaseException:
            terminate_owned_process_group(process)
            raise
    wall_seconds = time.monotonic() - started
    if returncode != 0:
        terminate_owned_process_group(process)
        tail = log_path.read_text(encoding="utf-8", errors="replace").splitlines()[-40:]
        fail(f"{label} failed with status {returncode}\n" + "\n".join(tail))
    result: dict[str, Any] = {
        "wall_seconds": wall_seconds,
        "peak_process_tree_rss_bytes": peak_rss,
        "peak_observed_process_tree_vm_hwm_sum_bytes": peak_hwm,
        "peak_observed_process_tree_locked_bytes": peak_locked,
        "peak_observed_process_tree_pinned_bytes": peak_pinned,
        "peak_process_count": peak_processes,
        "rss_sample_count": rss_samples,
        "sampling_interval_ms": 100,
        "log": log_path.name,
    }
    if monitor_gpu:
        result.update({
            "gpu_memory_baseline_mib": baseline_gpu,
            "gpu_memory_peak_mib": peak_gpu,
            "gpu_memory_peak_delta_mib": max(0, int(peak_gpu) - int(baseline_gpu)),
            "gpu_memory_sample_count": gpu_samples,
            "gpu_memory_sampling_interval_ms": 250,
            "gpu_memory_scope": (
                "device-wide WDDM observation, not a per-process allocation "
                "counter"),
        })
    return result


def query_hardware() -> dict[str, Any]:
    result = subprocess.run(
        ["nvidia-smi", "--query-gpu=name,driver_version,memory.total",
         "--format=csv,noheader,nounits"], text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5,
        check=False)
    if result.returncode != 0:
        fail("cannot query GPU hardware: " + result.stderr.strip())
    line = result.stdout.strip().splitlines()[0]
    fields = [item.strip() for item in line.split(",")]
    if len(fields) != 3:
        fail(f"unexpected nvidia-smi hardware row: {line!r}")
    try:
        total_mib = int(fields[2])
    except ValueError as exc:
        fail(f"invalid GPU memory total {fields[2]!r}: {exc}")
    memlock_soft, memlock_hard = resource.getrlimit(resource.RLIMIT_MEMLOCK)
    return {
        "gpu": fields[0],
        "driver_version": fields[1],
        "gpu_memory_total_mib": total_mib,
        "host_platform": platform.platform(),
        "host_machine": platform.machine(),
        "rlimit_memlock_soft_bytes": memlock_soft,
        "rlimit_memlock_hard_bytes": memlock_hard,
    }


def validate_repository(repo: Path, workspace: Path) -> None:
    head = git(repo, ["rev-parse", "HEAD"], "resolve parent revision")
    ancestry = subprocess.run(
        ["git", "-C", str(repo), "merge-base", "--is-ancestor",
         M8_A2_START_PARENT, head], check=False)
    if ancestry.returncode != 0:
        fail(f"M8-A2 start parent {M8_A2_START_PARENT} is not an ancestor of {head}")
    aircraft = git(repo / "external/Aircraft-Simu", ["rev-parse", "HEAD"],
                   "resolve Aircraft-Simu revision")
    require_equal("Aircraft-Simu revision", aircraft, AIRCRAFT_COMMIT)
    changes = git(repo / "external/Aircraft-Simu",
                  ["status", "--porcelain", "--untracked-files=no"],
                  "inspect Aircraft-Simu worktree")
    require_equal("Aircraft-Simu tracked worktree", changes, "")
    require_equal("M7 closure hash",
                  sha256_file(workspace / "validation/correctness_m7.json"),
                  M7_REPORT_SHA256)
    require_equal("M8-A1 receipt hash",
                  sha256_file(workspace / "validation/m8_receipts/m8_a1_inputs.json"),
                  M8_A1_RECEIPT_SHA256)


def load_calibration_policy(repo: Path, workspace: Path) -> dict[str, Any]:
    path = workspace / "validation" / CALIBRATION_POLICY_NAME
    policy = read_json(path)
    require_equal("calibration schema", policy.get("schema_version"), 1)
    require_equal("calibration phase", policy.get("phase"), "M8")
    require_equal("calibration segment", policy.get("segment"), "M8-A2")
    require_equal(
        "calibration ID", policy.get("calibration_id"),
        "uniform_seed12515_seeker_los_rate_z_adjacent_bin")
    scope = policy.get("scope", {})
    expected_scope = {
        "instance_counts": list(COUNTS), "instance_id": 10,
        "random_seed": 12515, "steps": STEPS, "dt_s": DT_S,
    }
    for key, expected in expected_scope.items():
        require_equal(f"calibration scope {key}", scope.get(key), expected)
    for name in ("scenario", "flight_control", "faults"):
        relative = scope.get(f"{name}_path")
        if not isinstance(relative, str):
            fail(f"calibration {name} path is missing")
        source = (repo / relative).resolve()
        try:
            source.relative_to(repo.resolve())
        except ValueError:
            fail(f"calibration {name} path escapes the repository: {relative}")
        require_equal(f"calibration {name} hash", sha256_file(source),
                      scope.get(f"{name}_sha256"))
    source = policy.get("source_quantization", {})
    expected_source = {
        "field": "SensorFrame.target_los_rate_ecef_meas.z",
        "wire_payload_index": 22, "predecessor_seq": 978,
        "packet_index": 979, "payload_seq": 979,
        "sim_time_s": 9.789999999999836, "resolution": 1.0e-7,
        "cpu_value": -0.0105284, "gpu_value": -0.0105285,
        "signed_bin_delta": -1,
        "required_unique_first_default_tolerance_mismatch": True,
        "round_input_probe": {
            "measurement": "scaled value passed to round(value / resolution)",
            "halfway_boundary": -105284.5,
            "cpu_scaled_value": -105284.48895384357,
            "cpu_offset_from_halfway_bins": 0.011046156432712451,
            "gpu_scaled_value": -105284.50445655835,
            "gpu_offset_from_halfway_bins": -0.0044565583521034569,
            "cpu_probe_result_bin": -105284,
            "gpu_probe_result_bin": -105285,
            "inputs_straddle_same_halfway_boundary": True,
        },
    }
    require_equal("calibration source contract", source, expected_source)
    probe = source["round_input_probe"]
    require_equal("CPU round probe bin",
                  -math.floor(abs(probe["cpu_scaled_value"]) + 0.5),
                  probe["cpu_probe_result_bin"])
    require_equal("GPU round probe bin",
                  -math.floor(abs(probe["gpu_scaled_value"]) + 0.5),
                  probe["gpu_probe_result_bin"])
    require_equal("CPU half-bin offset",
                  probe["cpu_scaled_value"] - probe["halfway_boundary"],
                  probe["cpu_offset_from_halfway_bins"])
    require_equal("GPU half-bin offset",
                  probe["gpu_scaled_value"] - probe["halfway_boundary"],
                  probe["gpu_offset_from_halfway_bins"])
    require_equal("CPU/GPU probes straddle half bin",
                  (probe["cpu_offset_from_halfway_bins"] > 0.0 and
                   probe["gpu_offset_from_halfway_bins"] < 0.0), True)
    hold = policy.get("command_hold", {})
    expected_hold = {
        "active_command_seq": 979, "hold_start_seq": 980,
        "hold_end_seq": 11999, "hold_mode": 6, "hold_status": 72,
        "range_at_hold_start_m": 0.0,
        "cpu_held_accel_ecef_mps2": [
            -8.556557072150724, 31.093278024693365, 22.56851081631039],
        "gpu_held_accel_ecef_mps2": [
            -8.556642423431372, 31.09357206262129, 22.568510816302226],
    }
    require_equal("calibration hold contract", hold, expected_hold)
    projection = policy.get("terminal_projection", {})
    expected_projection = {
        "fields": ["missile_x_ecef_m", "missile_y_ecef_m"],
        "cpu_xy_m": [-2854894.108834, 4911185.641251],
        "gpu_xy_m": [-2854894.545624323, 4911187.138164049],
        "signed_gpu_minus_cpu_xy_m": [
            -0.43679032335057855, 1.4969130493700504],
        "horizontal_difference_m": 1.5593378286782649,
        "delta_match_atol_m": 1.0e-9,
    }
    require_equal("calibration terminal projection contract", projection,
                  expected_projection)
    fma = policy.get("fma_diagnostic", {})
    expected_fma = {
        "diagnostic_option": "--fmad=false",
        "formal_build_uses_diagnostic_option": False,
        "baseline_gpu_trajectory_sha256": (
            "11da68682c0ad12121eb4c9bd84c4c4c34802a9917c1e5f92aee67faebd97337"),
        "all_fma_disabled_gpu_trajectory_sha256": (
            "11da68682c0ad12121eb4c9bd84c4c4c34802a9917c1e5f92aee67faebd97337"),
        "critical_los_rate_z_unchanged": -0.0105285,
        "terminal_gpu_xy_unchanged_m": [-2854894.545624323,
                                         4911187.138164049],
        "trajectory_outputs_identical": True,
        "conclusion": (
            "FMA is not the source of the adjacent quantization-bin difference."),
    }
    require_equal("calibration FMA diagnostic", fma, expected_fma)
    if "--fmad=false" in (workspace / "Makefile").read_text(
            encoding="utf-8"):
        fail("formal CUDA Makefile contains the diagnostic FMA option")
    guardrails = policy.get("guardrails", {})
    for key, expected in {
            "default_rtol": RTOL, "default_atol": ATOL,
            "allowed_calibrated_instances_per_batch": 1,
            "all_other_instances_use_default_tolerance": True,
            "gpu_repeat_must_be_exact": True,
            "n64_n128_prefix_must_be_exact": True,
            "full_trace_equivalence_claimed": False,
            "complete_final_state_claimed": False}.items():
        require_equal(f"calibration guardrail {key}", guardrails.get(key),
                      expected)
    if not isinstance(guardrails.get("reason"), str) or not guardrails["reason"]:
        fail("calibration reason is missing")
    scenario = read_json(repo / scope["scenario_path"])
    require_equal("calibration scenario LOS-rate resolution",
                  scenario["sensors"]["seeker"]["los_rate_noise"]["resolution"],
                  source["resolution"])
    return policy


def validate_frozen_runtime(path: Path, count: int) -> dict[str, Any]:
    runtime = read_json(path)
    require_equal(f"N={count} configured count",
                  runtime["campaign"]["instance_count"], count)
    require_equal(f"N={count} instance array", len(runtime["instances"]), count)
    require_equal(f"N={count} schedule", runtime["campaign"]["schedule"],
                  "PARALLEL")
    require_equal(f"N={count} CPU parallelism",
                  runtime["campaign"]["max_parallel_instances"], 6)
    require_equal(f"N={count} binary logging",
                  runtime["logging"]["binary_logs"], True)
    expected_scenario = (
        "gpu_workspace/configs/m8/generated/scenarios/uniform_long.json")
    for index, instance in enumerate(runtime["instances"]):
        require_equal(f"N={count} instance {index} ID", instance["instance_id"],
                      index)
        require_equal(f"N={count} instance {index} seed", instance["random_seed"],
                      12345 + 17 * index)
        require_equal(f"N={count} instance {index} scenario",
                      instance["scenario"], expected_scenario)
        require_equal(f"N={count} instance {index} faults",
                      instance["faults"],
                      "gpu_workspace/configs/m8/generated/faults/empty.json")
    return runtime


def materialize_runtime(source: Path, destination: Path,
                        output_dir: Path, count: int) -> None:
    runtime = validate_frozen_runtime(source, count)
    derived = copy.deepcopy(runtime)
    derived["logging"]["output_dir"] = str(output_dir.resolve())
    write_json_exclusive(destination, derived)
    reread = read_json(destination)
    expected = copy.deepcopy(runtime)
    expected["logging"]["output_dir"] = str(output_dir.resolve())
    require_equal(f"N={count} derived runtime", reread, expected)


def materialize_calibration_runtime(source: Path, destination: Path,
                                    output_dir: Path,
                                    policy: dict[str, Any]) -> None:
    runtime = validate_frozen_runtime(source, 64)
    scope = policy["scope"]
    instance = copy.deepcopy(runtime["instances"][scope["instance_id"]])
    require_equal("calibration instance ID", instance["instance_id"],
                  scope["instance_id"])
    require_equal("calibration random seed", instance["random_seed"],
                  scope["random_seed"])
    derived = copy.deepcopy(runtime)
    derived["campaign"]["campaign_id"] = "m8_a2_seed12515_full_diagnostic"
    derived["campaign"]["instance_count"] = 1
    derived["campaign"]["max_parallel_instances"] = 1
    derived["logging"]["output_dir"] = str(output_dir.resolve())
    derived["instances"] = [instance]
    write_json_exclusive(destination, derived)
    require_equal("derived calibration runtime", read_json(destination),
                  derived)


def validate_campaign(directory: Path, count: int, gpu: bool) -> dict[str, Any]:
    if not directory.is_dir():
        fail(f"campaign output is missing: {directory}")
    expected_names = {f"instance_{index:04d}" for index in range(count)}
    actual_names = {path.name for path in directory.glob("instance_[0-9][0-9][0-9][0-9]")
                    if path.is_dir()}
    require_equal(f"{directory.name} instance directories", actual_names,
                  expected_names)
    campaign = read_json(directory / "campaign_summary.json")
    require_equal(f"{directory.name} campaign instance count",
                  campaign["instance_count"], count)
    require_equal(f"{directory.name} campaign schedule",
                  campaign["schedule"], "PARALLEL")
    require_equal(f"{directory.name} completed count",
                  campaign["completed_count"], count)
    require_equal(f"{directory.name} failed count", campaign["failed_count"], 0)
    require_equal(f"{directory.name} summary count",
                  campaign["summary_available_count"], count)
    require_equal(f"{directory.name} hit count", campaign["hit_count"], 0)
    for key in ("total_fault_start_count", "total_fault_end_count",
                "total_fault_sensor_affected_step_count",
                "total_fault_actuator_affected_step_count",
                "aero_model_flags_or", "model_degradation_flags_or",
                "total_aero_extrapolated_sample_count"):
        require_equal(f"{directory.name} {key}", campaign[key], 0)
    require_equal(f"{directory.name} campaign rows", len(campaign["instances"]),
                  count)
    diagnostic_total = 0
    termination_counts = {"timeout": 0, "surface_collision": 0}
    for index in range(count):
        instance = directory / f"instance_{index:04d}"
        manifest = read_json(instance / "run_manifest.json")
        summary = read_json(instance / "summary.json")
        require_equal(f"instance {index} manifest ID", manifest["instance_id"],
                      index)
        require_equal(f"instance {index} seed", manifest["random_seed"],
                      12345 + 17 * index)
        require_equal(f"instance {index} dt", float(manifest["dt_s"]), DT_S)
        require_equal(f"instance {index} max time",
                      float(manifest["max_time_s"]), STEPS * DT_S)
        exit_reason = summary["exit_reason"]
        if exit_reason not in termination_counts:
            fail(f"instance {index} unexpected exit reason: {exit_reason!r}")
        steps = int(summary["simulation_steps"])
        if steps <= 0 or steps > STEPS:
            fail(f"instance {index} invalid terminal step: {steps}")
        if exit_reason == "timeout" and steps != STEPS:
            fail(f"instance {index} timeout at step {steps}, expected {STEPS}")
        if exit_reason == "surface_collision" and steps >= STEPS:
            fail(f"instance {index} collision did not terminate early: {steps}")
        termination_counts[exit_reason] += 1
        require_equal(f"instance {index} hit", summary["hit_flag"], False)
        require_equal(f"instance {index} samples",
                      summary["diagnostic_sample_count"], steps + 1)
        diagnostic_total += steps + 1
        row = campaign["instances"][index]
        require_equal(f"campaign row {index} ID", row["instance_id"], index)
        require_equal(f"campaign row {index} seed", row["random_seed"],
                      12345 + 17 * index)
        require_equal(f"campaign row {index} environment status",
                      row["env_status"], 0)
        require_equal(f"campaign row {index} flight-control status",
                      row["fc_status"], 0)
        require_equal(f"campaign row {index} exit", row["exit_reason"],
                      exit_reason)
        require_equal(f"campaign row {index} hit", row["hit_flag"], False)
        require_equal(f"campaign row {index} samples",
                      row["diagnostic_sample_count"], steps + 1)
        for key in ("fault_configured_count", "fault_start_count",
                    "fault_end_count", "fault_active_step_count",
                    "fault_sensor_affected_step_count",
                    "fault_actuator_affected_step_count",
                    "fault_max_concurrent_active"):
            require_equal(f"instance {index} {key}", summary[key], 0)
        for key in ("aero_model_flags_or", "model_degradation_flags_or",
                    "aero_extrapolated_sample_count"):
            require_equal(f"instance {index} {key}", summary[key], 0)
        event_path = instance / "event_log.txt"
        if not event_path.is_file():
            fail(f"instance {index} event log is missing")
        event_lines = event_path.read_text(
            encoding="utf-8", errors="strict").splitlines()
        terminal_time = steps * DT_S
        expected_events = [
            "0.000000 level=INFO event=SIMULATION_START detail=lockstep"]
        if exit_reason == "surface_collision":
            expected_events.append(
                f"{terminal_time:.6f} level=INFO "
                "event=SURFACE_COLLISION detail=agl_non_positive")
        expected_events.append(
            f"{terminal_time:.6f} level=INFO event=SIMULATION_STOP "
            f"detail={exit_reason}")
        require_equal(f"instance {index} event timeline", event_lines,
                      expected_events)
        if gpu:
            for name in ("trajectory.csv", "trajectory_diagnostics.csv",
                         "sensor_log.bin", "command_log.bin",
                         "fc_internal_log.bin"):
                if (instance / name).exists():
                    fail(f"GPU summary unexpectedly emitted {instance / name}")
        else:
            for name in ("trajectory.csv", "trajectory_diagnostics.csv",
                         "sensor_log.bin", "command_log.bin",
                         "fc_internal_log.bin"):
                if not (instance / name).is_file():
                    fail(f"CPU full result is missing {instance / name}")
    require_equal(f"{directory.name} diagnostic total",
                  campaign["total_diagnostic_sample_count"], diagnostic_total)
    if termination_counts["timeout"] == 0 or \
            termination_counts["surface_collision"] == 0:
        fail(f"{directory.name} did not exercise both long-run terminal paths: "
             f"{termination_counts}")
    if gpu:
        execution = read_json(directory / "gpu_execution.json")
        require_equal("GPU trace mode", execution["trace_mode"], "summary")
        require_equal("GPU mapping", execution["mapping"], "packed")
        require_equal("GPU lanes", execution["lanes_per_block"], LANES)
        require_equal("GPU chunk steps", execution["chunk_steps"], CHUNK_STEPS)
        require_equal("GPU chunk count", execution["chunk_count"],
                      EXPECTED_CHUNKS)
        return execution
    return {}


def run_comparator(repo: Path, left: Path, right: Path, log_path: Path | None,
                   label: str) -> str:
    command = [sys.executable, str(repo / "compare_runs.py"), str(left),
               str(right), "--rtol", str(RTOL), "--atol", str(ATOL)]
    result = subprocess.run(command, cwd=repo, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            check=False)
    if result.returncode != 0:
        fail(f"{label} semantic comparison failed\n{result.stdout[-8000:]}")
    if log_path is not None:
        write_bytes_exclusive(log_path, result.stdout.encode("utf-8"))
    print(f"m8_a2: {label} PASS", flush=True)
    return result.stdout


def strip_timing(value: Any) -> Any:
    if isinstance(value, dict):
        return {key: strip_timing(item) for key, item in value.items()
                if key not in TIMING_KEYS}
    if isinstance(value, list):
        return [strip_timing(item) for item in value]
    return value


def semantic_digest(directory: Path, count: int, gpu: bool) -> str:
    payload: dict[str, Any] = {
        "campaign": strip_timing(read_json(directory / "campaign_summary.json")),
        "instances": [],
    }
    if gpu:
        payload["gpu_execution"] = strip_timing(
            read_json(directory / "gpu_execution.json"))
    for index in range(count):
        instance = directory / f"instance_{index:04d}"
        payload["instances"].append({
            "manifest": read_json(instance / "run_manifest.json"),
            "summary": read_json(instance / "summary.json"),
            "event_log_sha256": sha256_file(instance / "event_log.txt"),
        })
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":"),
                         ensure_ascii=True).encode("utf-8")
    return sha256_bytes(encoded)


def normalized_manifest(path: Path) -> dict[str, Any]:
    manifest = read_json(path)
    for key in ("runtime_path", "environment_port", "flight_control_port"):
        manifest.pop(key, None)
    return manifest


def validate_prefix(left: Path, right: Path, label: str,
                    full_output: bool) -> str:
    digest = hashlib.sha256()
    for index in range(64):
        left_instance = left / f"instance_{index:04d}"
        right_instance = right / f"instance_{index:04d}"
        require_equal(
            f"{label}: instance {index} normalized manifest",
            normalized_manifest(left_instance / "run_manifest.json"),
            normalized_manifest(right_instance / "run_manifest.json"))
        manifest_blob = json.dumps(
            normalized_manifest(left_instance / "run_manifest.json"),
            sort_keys=True, separators=(",", ":")).encode("utf-8")
        digest.update(str(index).encode("ascii"))
        digest.update(b"\0manifest\0")
        digest.update(sha256_bytes(manifest_blob).encode("ascii"))
        digest.update(b"\n")
        filenames = ["summary.json", "event_log.txt"]
        if full_output:
            filenames.extend(("trajectory.csv", "trajectory_diagnostics.csv",
                              "sensor_log.bin", "command_log.bin",
                              "fc_internal_log.bin"))
        for filename in filenames:
            path_left = left / f"instance_{index:04d}" / filename
            path_right = right / f"instance_{index:04d}" / filename
            left_sha = sha256_file(path_left)
            right_sha = sha256_file(path_right)
            if left_sha != right_sha:
                fail(f"{label}: instance {index} {filename} differs")
            digest.update(str(index).encode("ascii"))
            digest.update(b"\0")
            digest.update(filename.encode("utf-8"))
            digest.update(b"\0")
            digest.update(left_sha.encode("ascii"))
            digest.update(b"\n")
    print(f"m8_a2: {label} 64-instance prefix PASS", flush=True)
    return digest.hexdigest()


def last_nonempty_line(path: Path) -> str:
    with path.open("rb") as stream:
        stream.seek(0, os.SEEK_END)
        end = stream.tell()
        start = max(0, end - 16384)
        stream.seek(start)
        lines = stream.read().splitlines()
    if not lines:
        fail(f"file has no records: {path}")
    return lines[-1].decode("utf-8")


def cpu_final_projection(directory: Path, count: int) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for index in range(count):
        instance = directory / f"instance_{index:04d}"
        trajectory = instance / "trajectory.csv"
        summary = read_json(instance / "summary.json")
        with trajectory.open("r", encoding="utf-8", newline="") as stream:
            header_line = stream.readline().rstrip("\r\n")
        header = next(csv.reader([header_line]))
        values = next(csv.reader([last_nonempty_line(trajectory)]))
        if len(header) != len(values):
            fail(f"invalid final trajectory row: {trajectory}")
        row = dict(zip(header, values))
        records.append({
            "instance_id": index,
            "steps": int(summary["simulation_steps"]),
            "final_time": float(row["time_s"]),
            "final_x": float(row["missile_x_ecef_m"]),
            "final_y": float(row["missile_y_ecef_m"]),
            "exit_reason": summary["exit_reason"],
            "status": 0,
            "hit": 0,
        })
    return records


def gpu_final_projection(log_path: Path, count: int) -> list[dict[str, Any]]:
    records: dict[int, dict[str, Any]] = {}
    for line in log_path.read_text(encoding="utf-8", errors="strict").splitlines():
        match = FINAL_STATE_RE.fullmatch(line)
        if match is None:
            continue
        instance = int(match.group("instance"))
        if instance in records:
            fail(f"duplicate GPU final-state record for instance {instance}: "
                 f"{log_path}")
        records[instance] = {
            "instance_id": instance,
            "steps": int(match.group("steps")),
            "final_time": float(match.group("time")),
            "final_x": float(match.group("x")),
            "final_y": float(match.group("y")),
            "exit_reason": match.group("exit"),
            "status": int(match.group("status")),
            "hit": int(match.group("hit")),
            "min_range": float(match.group("min_range")),
            "trace_records": int(match.group("trace_records")),
        }
    require_equal(f"{log_path.name} final-state record count", len(records),
                  count)
    require_equal(f"{log_path.name} final-state instance IDs",
                  sorted(records), list(range(count)))
    return [records[index] for index in range(count)]


def final_projection_digest(records: Sequence[dict[str, Any]]) -> str:
    return sha256_bytes(json.dumps(records, sort_keys=True,
                                   separators=(",", ":")).encode("utf-8"))


def validate_final_time_xy_projections(root: Path, count: int,
                                       policy: dict[str, Any]) -> dict[str, Any]:
    key = f"n{count}"
    cpu = cpu_final_projection(root / f"cpu/uniform_n{count}", count)
    primary = gpu_final_projection(root / f"logs/gpu_primary_{key}.log", count)
    repeat = gpu_final_projection(root / f"logs/gpu_repeat_{key}.log", count)
    require_equal(f"N={count} exact GPU final time/x/y projection repeat",
                  primary, repeat)
    max_abs_x = 0.0
    max_abs_y = 0.0
    max_default_horizontal = 0.0
    calibrated: list[dict[str, Any]] = []
    scope = policy["scope"]
    expected_delta = policy["terminal_projection"][
        "signed_gpu_minus_cpu_xy_m"]
    delta_atol = policy["terminal_projection"]["delta_match_atol_m"]
    for index, (cpu_row, gpu_row) in enumerate(zip(cpu, primary)):
        for key_name, expected in (
                ("steps", cpu_row["steps"]),
                ("exit_reason", cpu_row["exit_reason"]),
                ("status", 0), ("hit", 0), ("trace_records", 0)):
            require_equal(f"N={count} instance {index} GPU {key_name}",
                          gpu_row[key_name], expected)
        if not math.isclose(gpu_row["final_time"],
                            cpu_row["steps"] * DT_S,
                            rel_tol=RTOL, abs_tol=ATOL):
            fail(f"N={count} instance {index} GPU final time mismatch")
        if not math.isclose(cpu_row["final_time"], gpu_row["final_time"],
                            rel_tol=RTOL, abs_tol=ATOL):
            fail(f"N={count} instance {index} CPU/GPU final time mismatch")
        signed_delta = [gpu_row["final_x"] - cpu_row["final_x"],
                        gpu_row["final_y"] - cpu_row["final_y"]]
        close = [math.isclose(cpu_row[field], gpu_row[field],
                              rel_tol=RTOL, abs_tol=ATOL)
                 for field in ("final_x", "final_y")]
        if not all(close):
            if index != scope["instance_id"]:
                fail(f"N={count} instance {index} terminal projection needs "
                     "an unapproved calibration")
            require_equal(f"N={count} calibrated CPU terminal XY",
                          [cpu_row["final_x"], cpu_row["final_y"]],
                          policy["terminal_projection"]["cpu_xy_m"])
            require_equal(f"N={count} calibrated GPU terminal XY",
                          [gpu_row["final_x"], gpu_row["final_y"]],
                          policy["terminal_projection"]["gpu_xy_m"])
            for axis, actual, expected in zip(("x", "y"), signed_delta,
                                               expected_delta):
                if not math.isclose(actual, expected, rel_tol=0.0,
                                    abs_tol=delta_atol):
                    fail(f"N={count} calibrated {axis} delta mismatch: "
                         f"expected {expected!r}, got {actual!r}")
            calibrated.append({
                "instance_id": index,
                "random_seed": scope["random_seed"],
                "signed_gpu_minus_cpu_xy_m": signed_delta,
                "horizontal_difference_m": math.hypot(*signed_delta),
            })
        else:
            max_default_horizontal = max(
                max_default_horizontal, math.hypot(*signed_delta))
        max_abs_x = max(max_abs_x, abs(signed_delta[0]))
        max_abs_y = max(max_abs_y, abs(signed_delta[1]))
    require_equal(f"N={count} scoped calibration count", len(calibrated),
                  policy["guardrails"][
                      "allowed_calibrated_instances_per_batch"])
    print(f"m8_a2: N={count} final time/x/y projection PASS", flush=True)
    return {
        "fields": ["time_s", "missile_x_ecef_m", "missile_y_ecef_m"],
        "cpu_sha256": final_projection_digest(cpu),
        "gpu_primary_sha256": final_projection_digest(primary),
        "gpu_repeat_sha256": final_projection_digest(repeat),
        "gpu_repeat_exact": True,
        "default_tolerance_match_count": count - len(calibrated),
        "scoped_calibration_match_count": len(calibrated),
        "scoped_calibrations": calibrated,
        "max_default_tolerance_horizontal_difference_m":
            max_default_horizontal,
        "max_abs_x_difference_m": max_abs_x,
        "max_abs_y_difference_m": max_abs_y,
    }


def read_packet_payloads(path: Path, payload: struct.Struct, count: int,
                         instance_id: int) -> list[tuple[tuple[Any, ...],
                                                         tuple[Any, ...]]]:
    data = path.read_bytes()
    packet_size = PACKET_HEADER.size + payload.size
    require_equal(f"{path.name} byte size", len(data), count * packet_size)
    records = []
    for index in range(count):
        offset = index * packet_size
        header = PACKET_HEADER.unpack_from(data, offset)
        body = payload.unpack_from(data, offset + PACKET_HEADER.size)
        require_equal(f"{path.name} packet {index} instance", header[5],
                      instance_id)
        require_equal(f"{path.name} packet {index} header sequence", header[6],
                      index)
        require_equal(f"{path.name} packet {index} payload sequence", body[0],
                      index)
        require_equal(f"{path.name} packet {index} payload size", header[8],
                      payload.size)
        require_equal(f"{path.name} packet {index} header/payload time",
                      header[7], body[1])
        records.append((header, body))
    return records


def first_packet_mismatch(
        cpu: Sequence[tuple[tuple[Any, ...], tuple[Any, ...]]],
        gpu: Sequence[tuple[tuple[Any, ...], tuple[Any, ...]]],
        integer_payload_indexes: set[int]) -> tuple[int, list[int]] | None:
    for sequence, ((cpu_header, cpu_body),
                   (gpu_header, gpu_body)) in enumerate(zip(cpu, gpu)):
        for index in range(9):
            if index == 7:
                equal = math.isclose(cpu_header[index], gpu_header[index],
                                     rel_tol=RTOL, abs_tol=ATOL)
            else:
                equal = cpu_header[index] == gpu_header[index]
            if not equal:
                fail(f"packet {sequence} semantic header field {index} differs")
        mismatches: list[int] = []
        for index, (cpu_value, gpu_value) in enumerate(zip(cpu_body, gpu_body)):
            if index in integer_payload_indexes:
                equal = cpu_value == gpu_value
            else:
                equal = math.isclose(cpu_value, gpu_value, rel_tol=RTOL,
                                     abs_tol=ATOL)
            if not equal:
                mismatches.append(index)
        if mismatches:
            return sequence, mismatches
    return None


def validate_calibration_binary_evidence(cpu_instance: Path,
                                         gpu_instance: Path,
                                         policy: dict[str, Any]) -> dict[str, Any]:
    scope = policy["scope"]
    source = policy["source_quantization"]
    hold = policy["command_hold"]
    cpu_sensor = read_packet_payloads(
        cpu_instance / "sensor_log.bin", SENSOR_PAYLOAD, STEPS,
        scope["instance_id"])
    gpu_sensor = read_packet_payloads(
        gpu_instance / "sensor_log.bin", SENSOR_PAYLOAD, STEPS,
        scope["instance_id"])
    first_sensor = first_packet_mismatch(
        cpu_sensor, gpu_sensor, {0, 24, 25})
    require_equal("calibration first sensor mismatch", first_sensor,
                  (source["packet_index"], [source["wire_payload_index"]]))
    sequence = source["packet_index"]
    cpu_value = cpu_sensor[sequence][1][source["wire_payload_index"]]
    gpu_value = gpu_sensor[sequence][1][source["wire_payload_index"]]
    require_equal("calibration CPU quantized value", cpu_value,
                  source["cpu_value"])
    require_equal("calibration GPU quantized value", gpu_value,
                  source["gpu_value"])
    require_equal("calibration sensor payload sequence",
                  cpu_sensor[sequence][1][0], source["payload_seq"])
    require_equal("calibration sensor time", cpu_sensor[sequence][1][1],
                  source["sim_time_s"])
    resolution = source["resolution"]
    cpu_bin = round(cpu_value / resolution)
    gpu_bin = round(gpu_value / resolution)
    require_equal("calibration CPU value on quantization grid",
                  cpu_value, cpu_bin * resolution)
    require_equal("calibration GPU value on quantization grid",
                  gpu_value, gpu_bin * resolution)
    require_equal("calibration signed bin delta", gpu_bin - cpu_bin,
                  source["signed_bin_delta"])
    hold_start = hold["hold_start_seq"]
    require_equal("calibration CPU zero range at HOLD start",
                  cpu_sensor[hold_start][1][16],
                  hold["range_at_hold_start_m"])
    require_equal("calibration GPU zero range at HOLD start",
                  gpu_sensor[hold_start][1][16],
                  hold["range_at_hold_start_m"])

    cpu_command = read_packet_payloads(
        cpu_instance / "command_log.bin", COMMAND_PAYLOAD, STEPS,
        scope["instance_id"])
    gpu_command = read_packet_payloads(
        gpu_instance / "command_log.bin", COMMAND_PAYLOAD, STEPS,
        scope["instance_id"])
    first_command = first_packet_mismatch(
        cpu_command, gpu_command, {0, 19, 20})
    if first_command is None or first_command[0] != hold["active_command_seq"]:
        fail(f"calibration first command mismatch is not seq "
             f"{hold['active_command_seq']}: {first_command!r}")
    for label, records, expected_accel in (
            ("CPU", cpu_command, hold["cpu_held_accel_ecef_mps2"]),
            ("GPU", gpu_command, hold["gpu_held_accel_ecef_mps2"])):
        active = records[hold["active_command_seq"]][1]
        require_equal(f"calibration {label} active acceleration",
                      list(active[2:5]), expected_accel)
        for command_seq in range(hold_start, hold["hold_end_seq"] + 1):
            command = records[command_seq][1]
            require_equal(f"calibration {label} HOLD acceleration seq "
                          f"{command_seq}", list(command[2:5]),
                          expected_accel)
            require_equal(f"calibration {label} HOLD mode seq {command_seq}",
                          command[19], hold["hold_mode"])
            require_equal(f"calibration {label} HOLD status seq {command_seq}",
                          command[20], hold["hold_status"])
    return {
        "calibration_id": policy["calibration_id"],
        "first_sensor_default_tolerance_mismatch": {
            "packet_index": sequence,
            "payload_fields": first_sensor[1],
            "field": source["field"],
            "cpu_value": cpu_value,
            "gpu_value": gpu_value,
            "resolution": resolution,
            "signed_bin_delta": gpu_bin - cpu_bin,
            "round_input_probe": source["round_input_probe"],
        },
        "first_command_default_tolerance_mismatch": {
            "packet_index": first_command[0],
            "payload_fields": first_command[1],
        },
        "hold_chain": {
            "start_seq": hold_start,
            "end_seq": hold["hold_end_seq"],
            "mode": hold["hold_mode"],
            "status": hold["hold_status"],
            "verified_backend_count": 2,
        },
        "artifact_sha256": {
            "cpu_sensor_log": sha256_file(cpu_instance / "sensor_log.bin"),
            "gpu_sensor_log": sha256_file(gpu_instance / "sensor_log.bin"),
            "cpu_command_log": sha256_file(cpu_instance / "command_log.bin"),
            "gpu_command_log": sha256_file(gpu_instance / "command_log.bin"),
            "cpu_trajectory": sha256_file(cpu_instance / "trajectory.csv"),
            "gpu_trajectory": sha256_file(gpu_instance / "trajectory.csv"),
        },
        "fma_diagnostic": policy["fma_diagnostic"],
    }


def validate_calibration_campaign(root: Path,
                                  policy: dict[str, Any]) -> dict[str, Any]:
    directory = root / "gpu_diagnostic/seed12515_full"
    scope = policy["scope"]
    instance_name = f"instance_{scope['instance_id']:04d}"
    actual_names = {path.name for path in directory.glob("instance_[0-9][0-9][0-9][0-9]")
                    if path.is_dir()}
    require_equal("calibration diagnostic instance directory", actual_names,
                  {instance_name})
    campaign = read_json(directory / "campaign_summary.json")
    for key, expected in (("instance_count", 1), ("completed_count", 1),
                          ("failed_count", 0),
                          ("summary_available_count", 1)):
        require_equal(f"calibration diagnostic {key}", campaign[key], expected)
    gpu_instance = directory / instance_name
    cpu_instance = root / f"cpu/uniform_n64/{instance_name}"
    summary = read_json(gpu_instance / "summary.json")
    require_equal("calibration diagnostic exit", summary["exit_reason"],
                  "timeout")
    require_equal("calibration diagnostic steps", summary["simulation_steps"],
                  STEPS)
    reference = root / f"gpu/uniform_n64/{instance_name}"
    require_equal("calibration full/summary manifest",
                  normalized_manifest(gpu_instance / "run_manifest.json"),
                  normalized_manifest(reference / "run_manifest.json"))
    require_equal("calibration full/summary semantic summary",
                  strip_timing(summary),
                  strip_timing(read_json(reference / "summary.json")))
    require_equal("calibration CPU/GPU event log",
                  (gpu_instance / "event_log.txt").read_bytes(),
                  (cpu_instance / "event_log.txt").read_bytes())
    for name in ("trajectory.csv", "trajectory_diagnostics.csv",
                 "sensor_log.bin", "command_log.bin"):
        if not (gpu_instance / name).is_file():
            fail(f"calibration GPU full result is missing {name}")
    execution = read_json(directory / "gpu_execution.json")
    require_equal("calibration GPU trace mode", execution["trace_mode"],
                  "full")
    require_equal("calibration GPU mapping", execution["mapping"], "packed")
    require_equal("calibration GPU lanes", execution["lanes_per_block"], LANES)
    require_equal("calibration GPU chunk steps", execution["chunk_steps"],
                  CHUNK_STEPS)
    require_equal("calibration GPU chunk count", execution["chunk_count"],
                  EXPECTED_CHUNKS)
    terminal_rows = []
    for line in (root / "logs/gpu_calibration_seed12515_full.log").read_text(
            encoding="utf-8", errors="strict").splitlines():
        match = FINAL_STATE_RE.fullmatch(line)
        if match is not None:
            terminal_rows.append(match)
    require_equal("calibration diagnostic terminal row count",
                  len(terminal_rows), 1)
    terminal = terminal_rows[0]
    require_equal("calibration diagnostic terminal instance",
                  int(terminal.group("instance")), scope["instance_id"])
    require_equal("calibration diagnostic terminal steps",
                  int(terminal.group("steps")), STEPS)
    require_equal("calibration diagnostic terminal XY",
                  [float(terminal.group("x")), float(terminal.group("y"))],
                  policy["terminal_projection"]["gpu_xy_m"])
    require_equal("calibration diagnostic full trace record count",
                  int(terminal.group("trace_records")), STEPS + 1)
    return {
        "execution": execution,
        "evidence": validate_calibration_binary_evidence(
            cpu_instance, gpu_instance, policy),
    }


def source_evidence(repo: Path, workspace: Path) -> dict[str, Any]:
    paths = {
        "runner": workspace / "tools/run_m8_long_baseline.py",
        "matrix": workspace / "configs/m8/cases.json",
        "input_generator": workspace / "tools/prepare_m8_cases.py",
        "comparator": repo / "compare_runs.py",
        "cpu_build_script": workspace / "tools/build_cpu_baseline.sh",
        "gpu_makefile": workspace / "Makefile",
        "m8_a1_receipt": workspace / "validation/m8_receipts/m8_a1_inputs.json",
        "m7_closure_report": workspace / "validation/correctness_m7.json",
        "m8_a2_quantization_calibration":
            workspace / "validation" / CALIBRATION_POLICY_NAME,
        "uniform_runtime_n64":
            workspace / "configs/m8/generated/runtimes/uniform_n64.json",
        "uniform_runtime_n128":
            workspace / "configs/m8/generated/runtimes/uniform_n128.json",
        "uniform_scenario":
            workspace / "configs/m8/generated/scenarios/uniform_long.json",
        "empty_faults":
            workspace / "configs/m8/generated/faults/empty.json",
        "baseline_flight_control":
            repo / "external/Aircraft-Simu/configs/baseline/flight_control.json",
    }
    return {
        "sha256": {name: sha256_file(path) for name, path in paths.items()},
        "gpu_source_tree": tree_fingerprint(
            repo, [workspace / "gpu_campaign", workspace / "staging"]),
    }


def binary_evidence(workspace: Path) -> dict[str, str]:
    paths = {
        "gpu_campaign": workspace / "build/bin/gpu_campaign",
        "cpu_environment": workspace / "build/cpu_baseline/environment_sim",
        "cpu_flight_control": workspace / "build/cpu_baseline/flight_control_sim",
        "cpu_instance_manager": workspace / "build/cpu_baseline/instance_manager",
    }
    return {name: sha256_file(path) for name, path in paths.items()}


def allocation_budget(workspace: Path, hardware: dict[str, Any]) -> dict[str, Any]:
    a1 = read_json(workspace / "validation/m8_receipts/m8_a1_inputs.json")
    source = a1["resource_budget"]
    state_size = int(source["instance_state_bytes"])
    config_size = int(source["instance_config_bytes"])
    result: dict[str, Any] = {
        "scope": (
            "exact trace/state/config byte formulas plus device-wide WDDM "
            "sampling; CUDA context, events, streams, and shared resource "
            "allocator overhead are not attributed per process"),
        "trace_record_bytes": int(source["trace_record_bytes"]),
        "instance_state_bytes": state_size,
        "instance_config_bytes": config_size,
        "counts": {},
    }
    soft_limit = int(hardware["rlimit_memlock_soft_bytes"])
    for count in COUNTS:
        a1_count = source["counts"][str(count)]
        pinned = int(a1_count["chunk_256_double_pinned_trace_and_state_bytes"])
        device_trace_state = int(
            a1_count["chunk_256_device_trace_and_state_bytes"])
        result["counts"][str(count)] = {
            "raw_full_trace_reference_bytes": int(a1_count["trace_bytes"]),
            "chunk_256_cumulative_trace_d2h_bytes": (
                EXPECTED_CHUNKS * int(
                    a1_count["chunk_256_single_trace_buffer_bytes"])),
            "exact_device_trace_and_state_bytes": device_trace_state,
            "exact_device_instance_config_bytes": count * config_size,
            "known_device_request_lower_bound_bytes": (
                device_trace_state + count * config_size),
            "exact_host_pinned_trace_and_state_bytes": pinned,
            "host_state_vector_bytes": count * state_size,
            "pinned_request_exceeds_rlimit_memlock": (
                soft_limit != resource.RLIM_INFINITY and pinned > soft_limit),
            "allocation_result": "PASS (campaign exited zero; any CUDA allocation failure is fatal)",
        }
    return result


def validate_monitoring(monitoring: dict[str, Any]) -> None:
    runs = monitoring.get("runs")
    if not isinstance(runs, dict):
        fail("monitoring runs are missing")
    for count in COUNTS:
        count_runs = runs.get(f"n{count}")
        if not isinstance(count_runs, dict):
            fail(f"monitoring N={count} runs are missing")
        for name in ("cpu", "gpu_primary", "gpu_repeat"):
            observed = count_runs.get(name)
            if not isinstance(observed, dict):
                fail(f"monitoring N={count} {name} is missing")
            if int(observed.get("rss_sample_count", 0)) < 1:
                fail(f"monitoring N={count} {name} has no RSS samples")
            if int(observed.get("peak_process_tree_rss_bytes", 0)) <= 0:
                fail(f"monitoring N={count} {name} has no positive RSS peak")
            if int(observed.get("peak_process_count", 0)) <= 0:
                fail(f"monitoring N={count} {name} observed no process")
            if name.startswith("gpu"):
                if int(observed.get("gpu_memory_sample_count", 0)) < 1:
                    fail(f"monitoring N={count} {name} has no GPU samples")
                baseline = int(observed.get("gpu_memory_baseline_mib", -1))
                peak = int(observed.get("gpu_memory_peak_mib", -1))
                if baseline < 0 or peak < baseline:
                    fail(f"monitoring N={count} {name} GPU memory is invalid")
    diagnostic = monitoring.get("calibration_gpu_full")
    if not isinstance(diagnostic, dict):
        fail("calibration GPU full monitoring is missing")
    if int(diagnostic.get("rss_sample_count", 0)) < 1 or \
            int(diagnostic.get("peak_process_count", 0)) < 1:
        fail("calibration GPU full monitoring has no process/RSS samples")
    baseline = int(diagnostic.get("gpu_memory_baseline_mib", -1))
    peak = int(diagnostic.get("gpu_memory_peak_mib", -1))
    if int(diagnostic.get("gpu_memory_sample_count", 0)) < 1 or \
            baseline < 0 or peak < baseline:
        fail("calibration GPU full monitoring has invalid GPU samples")


def termination_evidence(directory: Path, count: int) -> dict[str, Any]:
    sequence: list[dict[str, Any]] = []
    counts = {"timeout": 0, "surface_collision": 0}
    for index in range(count):
        summary = read_json(
            directory / f"instance_{index:04d}/summary.json")
        reason = summary["exit_reason"]
        if reason not in counts:
            fail(f"unexpected terminal reason in {directory}: {reason!r}")
        counts[reason] += 1
        sequence.append({
            "instance_id": index,
            "exit_reason": reason,
            "simulation_steps": int(summary["simulation_steps"]),
            "diagnostic_sample_count": int(
                summary["diagnostic_sample_count"]),
        })
    return {
        "counts": counts,
        "sequence_sha256": sha256_bytes(json.dumps(
            sequence, sort_keys=True, separators=(",", ":")).encode("utf-8")),
    }


def validate_all_results(repo: Path, root: Path, policy: dict[str, Any],
                         write_compare_logs: bool) -> dict[str, Any]:
    executions: dict[str, Any] = {}
    comparisons: dict[str, Any] = {}
    for count in COUNTS:
        key = f"n{count}"
        cpu = root / "cpu" / f"uniform_n{count}"
        gpu = root / "gpu" / f"uniform_n{count}"
        repeat = root / "gpu_repeat" / f"uniform_n{count}"
        validate_campaign(cpu, count, False)
        executions[key] = {
            "primary": validate_campaign(gpu, count, True),
            "repeat": validate_campaign(repeat, count, True),
        }
        cpu_termination = termination_evidence(cpu, count)
        primary_termination = termination_evidence(gpu, count)
        repeat_termination = termination_evidence(repeat, count)
        require_equal(f"N={count} CPU/GPU terminal sequence",
                      primary_termination, cpu_termination)
        require_equal(f"N={count} CPU/GPU repeat terminal sequence",
                      repeat_termination, cpu_termination)
        pairs = (("cpu_gpu", cpu, gpu),
                 ("cpu_gpu_repeat", cpu, repeat),
                 ("gpu_repeat", gpu, repeat))
        logs: dict[str, str] = {}
        for name, left, right in pairs:
            log_path = (root / "logs" / f"compare_{key}_{name}.log"
                        if write_compare_logs else None)
            output = run_comparator(repo, left, right, log_path,
                                    f"{key} {name}")
            logs[name] = (sha256_bytes(output.encode("utf-8")) if log_path is None
                          else sha256_file(log_path))
        primary_digest = semantic_digest(gpu, count, True)
        repeat_digest = semantic_digest(repeat, count, True)
        require_equal(f"N={count} exact normalized GPU determinism",
                      primary_digest, repeat_digest)
        comparisons[key] = {
            "matched_instances": count,
            "comparator_log_sha256": logs,
            "gpu_primary_semantic_sha256": primary_digest,
            "gpu_repeat_semantic_sha256": repeat_digest,
            "exact_normalized_gpu_repeat": True,
            "termination": cpu_termination,
            "final_time_xy_projection": validate_final_time_xy_projections(
                root, count, policy),
        }
    comparisons["prefix_invariance"] = {
        "cpu_n64_vs_n128": validate_prefix(
            root / "cpu/uniform_n64", root / "cpu/uniform_n128", "CPU",
            True),
        "gpu_n64_vs_n128": validate_prefix(
            root / "gpu/uniform_n64", root / "gpu/uniform_n128",
            "GPU primary", False),
        "gpu_repeat_n64_vs_n128": validate_prefix(
            root / "gpu_repeat/uniform_n64",
            root / "gpu_repeat/uniform_n128", "GPU repeat", False),
    }
    for run_name, log_prefix in (("gpu_primary", "gpu_primary"),
                                 ("gpu_repeat", "gpu_repeat")):
        n64 = gpu_final_projection(root / f"logs/{log_prefix}_n64.log", 64)
        n128 = gpu_final_projection(root / f"logs/{log_prefix}_n128.log", 128)
        require_equal(f"{run_name} N=64/N=128 final projection prefix",
                      n64, n128[:64])
        comparisons["prefix_invariance"][
            f"{run_name}_final_projection"] = final_projection_digest(n64)
    calibration = validate_calibration_campaign(root, policy)
    executions["calibration_gpu_full"] = calibration["execution"]
    comparisons["scoped_quantization_calibration"] = calibration["evidence"]
    return {"executions": executions, "comparisons": comparisons}


def make_receipt(repo: Path, workspace: Path, root: Path,
                 monitoring: dict[str, Any], result_validation: dict[str, Any],
                 hardware: dict[str, Any]) -> dict[str, Any]:
    cuda_versions = {
        str(result_validation["executions"][f"n{count}"][run][
            "cuda_runtime_version"])
        for count in COUNTS for run in ("primary", "repeat")
    }
    cuda_versions.add(str(result_validation["executions"][
        "calibration_gpu_full"]["cuda_runtime_version"]))
    if len(cuda_versions) != 1:
        fail(f"GPU runs used inconsistent CUDA runtime versions: {cuda_versions}")
    hardware_record = dict(hardware)
    hardware_record["cuda_runtime_version"] = int(next(iter(cuda_versions)))
    return {
        "schema_version": 1,
        "phase": "M8",
        "segment": "M8-A2",
        "status": "PASS",
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "complete_m8_gate": False,
        "next_segment": "M8-A3",
        "source": {
            "parent_commit": git(repo, ["rev-parse", "HEAD"],
                                 "resolve parent revision"),
            "m8_a2_start_parent_commit": M8_A2_START_PARENT,
            "aircraft_simu_commit": AIRCRAFT_COMMIT,
            "aircraft_simu_tracked_worktree_clean": True,
            "m7_closure_sha256": M7_REPORT_SHA256,
            "m8_a1_receipt_sha256": M8_A1_RECEIPT_SHA256,
            **source_evidence(repo, workspace),
            "binary_sha256": binary_evidence(workspace),
        },
        "contract": {
            "profile": "uniform long-noise with empty faults",
            "instance_counts": list(COUNTS),
            "steps": STEPS,
            "dt_s": DT_S,
            "max_time_s": STEPS * DT_S,
            "expected_exit_reasons": ["timeout", "surface_collision"],
            "termination_oracle": (
                "pinned CPU baseline; each frozen seed must match the GPU "
                "exit reason and terminal step exactly"),
            "cpu_schedule": "PARALLEL",
            "cpu_max_parallel_instances": 6,
            "gpu_trace_mode": "summary",
            "gpu_mapping": "packed",
            "gpu_lanes_per_block": LANES,
            "gpu_chunk_steps": CHUNK_STEPS,
            "gpu_expected_chunk_count": EXPECTED_CHUNKS,
            "rtol": RTOL,
            "atol": ATOL,
            "scoped_quantization_calibration": {
                "policy": f"gpu_workspace/validation/{CALIBRATION_POLICY_NAME}",
                "calibration_id": (
                    result_validation["comparisons"]
                    ["scoped_quantization_calibration"]["calibration_id"]),
                "applies_to_one_frozen_instance_per_batch": True,
                "all_other_instances_use_default_tolerance": True,
                "full_trace_equivalence_claimed": False,
            },
            "direct_rng_state_observed": False,
            "rng_continuity_claimed": False,
            "rng_coverage": (
                "indirect output-level evidence through the noisy-scenario "
                "CPU/GPU summaries and final time/x/y projection, exact GPU "
                "repeat, and N=64/N=128 output/projection prefix invariance; "
                "internal PRNG states and draw counts are not observed"),
            "complete_final_state_claimed": False,
            "final_state_coverage": (
                "final time and missile ECEF x/y projection only; complete "
                "terminal state is not exposed by production summary mode"),
        },
        "hardware": hardware_record,
        "allocation_budget": allocation_budget(workspace, hardware_record),
        "monitoring": monitoring,
        "execution_metadata": result_validation["executions"],
        "comparisons": result_validation["comparisons"],
        "artifacts": {
            "derived_inputs": tree_fingerprint(root, [root / "runtimes"]),
            "results": tree_fingerprint(
                root, [root / "cpu", root / "gpu", root / "gpu_repeat",
                       root / "gpu_diagnostic"]),
            "logs": tree_fingerprint(root, [root / "logs"]),
            "monitoring_sha256": sha256_file(root / "monitoring.json"),
            "cpu_campaign_count": len(list((root / "cpu").glob(
                "*/campaign_summary.json"))),
            "gpu_campaign_count": len(list((root / "gpu").glob(
                "*/campaign_summary.json"))) + len(list(
                    (root / "gpu_repeat").glob("*/campaign_summary.json"))) +
                len(list((root / "gpu_diagnostic").glob(
                    "*/campaign_summary.json"))),
            "gpu_execution_count": len(list(root.glob(
                "gpu*/**/gpu_execution.json"))),
        },
        "validation": {
            "cpu_gpu_semantic_comparison": "PASS",
            "gpu_repeat_semantic_comparison": "PASS",
            "exact_normalized_gpu_repeat": "PASS",
            "n64_n128_prefix_invariance": "PASS",
            "terminal_steps_reasons_and_flags": "PASS",
            "cpu_gpu_final_time_xy_projection":
                "PASS_WITH_ONE_SCOPED_CALIBRATION_PER_BATCH",
            "scoped_quantization_diagnostic": "PASS",
            "rng_dependent_output_regression_indirect": "PASS",
            "allocation_and_execution": "PASS",
            "host_rss_and_device_wide_memory_observed": "PASS",
            "performance_claimed": False,
        },
    }


def validate_receipts(local: Path, tracked: Path, expected: dict[str, Any],
                      repo: Path) -> str:
    if not tracked.is_file():
        fail("tracked M8-A2 receipt is missing")
    if not local.is_file():
        fail("build-local M8-A2 receipt is missing")
    tracked_bytes = tracked.read_bytes()
    require_equal("local/tracked M8-A2 receipt", local.read_bytes(),
                  tracked_bytes)
    actual = read_json(tracked)
    timestamp_value = actual.get("created_at_utc")
    try:
        timestamp = datetime.fromisoformat(str(timestamp_value))
    except ValueError as exc:
        fail(f"invalid receipt timestamp: {exc}")
    if timestamp.tzinfo is None:
        fail("receipt timestamp must be timezone-aware")
    parent = actual.get("source", {}).get("parent_commit")
    if not isinstance(parent, str) or len(parent) != 40:
        fail("receipt parent commit is invalid")
    git(repo, ["cat-file", "-e", parent + "^{commit}"],
        "resolve receipt parent")
    head = git(repo, ["rev-parse", "HEAD"], "resolve current parent")
    ancestry = subprocess.run(
        ["git", "-C", str(repo), "merge-base", "--is-ancestor", parent, head],
        check=False)
    if ancestry.returncode != 0:
        fail(f"receipt parent {parent} is not an ancestor of {head}")
    rebuilt = copy.deepcopy(expected)
    rebuilt["created_at_utc"] = timestamp_value
    rebuilt["source"]["parent_commit"] = parent
    # A receipt attests the executable and checker used for its historical
    # results.  NVCC without --frandom-seed can change object/binary bytes on
    # a source-identical rebuild.  A saved-result check must retain those
    # historical digests, while still checking all inputs, source-tree content,
    # result files, and semantic comparisons against the current code.
    recorded_source = actual.get("source", {})
    if not isinstance(recorded_source, dict):
        fail("receipt source provenance is invalid")
    recorded_sha = recorded_source.get("sha256", {})
    if not isinstance(recorded_sha, dict):
        fail("receipt source hashes are invalid")
    recorded_runner = recorded_sha.get("runner")
    if not isinstance(recorded_runner, str) or not re.fullmatch(
            r"[0-9a-f]{64}", recorded_runner):
        fail("receipt runner hash is invalid")
    current_runner = rebuilt["source"]["sha256"]["runner"]
    if recorded_runner != current_runner:
        print("m8_a2: historical runner hash differs from current checker; "
              "saved-result validation uses the recorded runner provenance",
              flush=True)
    rebuilt["source"]["sha256"]["runner"] = recorded_runner
    recorded_binaries = recorded_source.get("binary_sha256", {})
    current_binaries = rebuilt["source"]["binary_sha256"]
    if not isinstance(recorded_binaries, dict) or (
            set(recorded_binaries) != set(current_binaries)):
        fail("receipt binary provenance is invalid")
    for name, recorded_hash in recorded_binaries.items():
        if not isinstance(recorded_hash, str) or not re.fullmatch(
                r"[0-9a-f]{64}", recorded_hash):
            fail(f"receipt {name} binary hash is invalid")
        if recorded_hash != current_binaries[name]:
            print(f"m8_a2: historical {name} binary differs from the "
                  "current build; current executable is not attested by "
                  "this receipt", flush=True)
    rebuilt["source"]["binary_sha256"] = recorded_binaries
    require_equal("complete M8-A2 receipt semantics", actual, rebuilt)
    return sha256_bytes(tracked_bytes)


def run_segment(repo: Path, workspace: Path, root: Path,
                tracked_receipt: Path, skip_build: bool,
                policy: dict[str, Any]) -> str:
    if root.exists():
        fail(f"output already exists; refusing to replace it: {root}")
    if tracked_receipt.exists():
        fail(f"tracked receipt already exists: {tracked_receipt}")
    disk_before = shutil.disk_usage(workspace)
    if disk_before.free < MINIMUM_FREE_BYTES:
        fail(f"M8-A2 requires at least {MINIMUM_FREE_BYTES} free bytes; "
             f"only {disk_before.free} are available")
    hardware = query_hardware()
    root.mkdir(parents=True)
    (root / "logs").mkdir()
    (root / "runtimes").mkdir()
    for directory in (root / "cpu", root / "gpu", root / "gpu_repeat",
                      root / "gpu_diagnostic"):
        directory.mkdir()

    run_logged([sys.executable, str(workspace / "tools/prepare_m8_cases.py"),
                "--check"], repo, root / "logs/m8_a1_check.log",
               "revalidate M8-A1 inputs")
    if not skip_build:
        run_logged([str(workspace / "tools/build_cpu_baseline.sh")], repo,
                   root / "logs/build_cpu.log", "build CPU baseline")
        run_logged(["make", "-C", str(workspace), "-j2"], repo,
                   root / "logs/build_gpu.log", "build GPU campaign")

    for count in COUNTS:
        source = workspace / f"configs/m8/generated/runtimes/uniform_n{count}.json"
        destination = root / f"runtimes/uniform_n{count}.json"
        materialize_runtime(source, destination,
                            root / f"cpu/uniform_n{count}", count)
    calibration_runtime = root / "runtimes/seed12515_full.json"
    materialize_calibration_runtime(
        workspace / "configs/m8/generated/runtimes/uniform_n64.json",
        calibration_runtime, root / "gpu_diagnostic/seed12515_full", policy)

    monitoring: dict[str, Any] = {
        "method": {
            "host": (
                "100 ms /proc process-tree aggregate VmRSS sampling with "
                "observed VmHWM/VmLck/VmPin sums"),
            "device": (
                "250 ms nvidia-smi device-wide WDDM memory.used sampling; "
                "delta is relative to the immediately preceding baseline "
                "and is not a per-process allocation counter"),
            "timing_use": "diagnostic only; sampling perturbs wall time and this segment makes no performance claim",
        },
        "disk_free_before_bytes": disk_before.free,
        "runs": {},
    }
    cpu_binary = workspace / "build/cpu_baseline/instance_manager"
    gpu_binary = workspace / "build/bin/gpu_campaign"
    for count in COUNTS:
        key = f"n{count}"
        runtime = root / f"runtimes/uniform_n{count}.json"
        cpu_output = root / f"cpu/uniform_n{count}"
        gpu_output = root / f"gpu/uniform_n{count}"
        repeat_output = root / f"gpu_repeat/uniform_n{count}"
        monitoring["runs"][key] = {}
        monitoring["runs"][key]["cpu"] = run_monitored(
            [str(cpu_binary), "--runtime", str(runtime)], repo,
            root / f"logs/cpu_{key}.log", f"CPU uniform N={count}", False)
        for name, output in (("gpu_primary", gpu_output),
                             ("gpu_repeat", repeat_output)):
            monitoring["runs"][key][name] = run_monitored(
                [str(gpu_binary), "--config", str(runtime), "--output-dir",
                 str(output), "--trace-mode", "summary", "--chunk-steps",
                 str(CHUNK_STEPS), "--mapping", "packed",
                 "--lanes-per-block", str(LANES)], repo,
                root / f"logs/{name}_{key}.log",
                f"{name} uniform N={count}", True)

    monitoring["calibration_gpu_full"] = run_monitored(
        [str(gpu_binary), "--config", str(calibration_runtime),
         "--output-dir", str(root / "gpu_diagnostic/seed12515_full"),
         "--trace-mode", "full", "--chunk-steps", str(CHUNK_STEPS),
         "--mapping", "packed", "--lanes-per-block", str(LANES)], repo,
        root / "logs/gpu_calibration_seed12515_full.log",
        "GPU calibration diagnostic instance=10 seed=12515 full", True)

    monitoring["disk_free_after_bytes"] = shutil.disk_usage(
        workspace / "build").free
    validate_monitoring(monitoring)
    write_json_exclusive(root / "monitoring.json", monitoring)
    result_validation = validate_all_results(repo, root, policy, True)
    receipt = make_receipt(repo, workspace, root, monitoring,
                           result_validation, hardware)
    local_receipt = root / "m8_a2_pass_receipt.json"
    receipt_blob = json_bytes(receipt)
    write_bytes_exclusive(local_receipt, receipt_blob)
    require_equal("new local receipt bytes", local_receipt.read_bytes(),
                  receipt_blob)
    require_equal("new local receipt semantics", read_json(local_receipt),
                  receipt)
    # The tracked PASS marker is the final publication step.  No validation
    # capable of failing is performed after this exclusive, fsync-backed write.
    write_bytes_exclusive(tracked_receipt, receipt_blob)
    receipt_sha = sha256_bytes(receipt_blob)
    print("m8_a2: PASS uniform N=64/128 12000-step CPU/GPU summary baseline",
          flush=True)
    return receipt_sha


def check_segment(repo: Path, workspace: Path, root: Path,
                  tracked_receipt: Path,
                  policy: dict[str, Any]) -> str:
    if not root.is_dir():
        fail(f"M8-A2 output is missing: {root}")
    validate_repository(repo, workspace)
    converted_delta = subprocess.run(
        ["git", "-C", str(repo), "diff", "--quiet", M8_A2_START_PARENT,
         "--", "gpu_workspace/converted"], check=False)
    if converted_delta.returncode != 0:
        fail("converted CUDA sources differ from the M8-A2 source baseline")
    monitoring = read_json(root / "monitoring.json")
    validate_monitoring(monitoring)
    check = subprocess.run(
        [sys.executable, str(workspace / "tools/prepare_m8_cases.py"),
         "--check"], cwd=repo, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False)
    if check.returncode != 0:
        fail("M8-A1 input revalidation failed during M8-A2 check\n" +
             check.stdout[-8000:])
    historical = read_json(tracked_receipt)
    hardware = historical.get("hardware")
    if not isinstance(hardware, dict):
        fail("tracked M8-A2 receipt has no historical hardware record")
    result_validation = validate_all_results(repo, root, policy, False)
    expected = make_receipt(repo, workspace, root, monitoring,
                            result_validation, hardware)
    return validate_receipts(root / "m8_a2_pass_receipt.json",
                             tracked_receipt, expected, repo)


def resume_existing_segment(repo: Path, workspace: Path, root: Path,
                            tracked_receipt: Path,
                            policy: dict[str, Any]) -> str:
    """Validate a fully executed run and publish its receipt without rerunning."""
    if not root.is_dir():
        fail(f"M8-A2 output is missing: {root}")
    if tracked_receipt.exists() or (root / "m8_a2_pass_receipt.json").exists():
        fail("refusing to replace an existing M8-A2 receipt")
    validate_repository(repo, workspace)
    check = subprocess.run(
        [sys.executable, str(workspace / "tools/prepare_m8_cases.py"),
         "--check"], cwd=repo, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False)
    if check.returncode != 0:
        fail("M8-A1 input revalidation failed during M8-A2 resume\n" +
             check.stdout[-8000:])
    monitoring = read_json(root / "monitoring.json")
    validate_monitoring(monitoring)
    hardware = query_hardware()
    result_validation = validate_all_results(repo, root, policy, False)
    receipt = make_receipt(repo, workspace, root, monitoring,
                           result_validation, hardware)
    local_receipt = root / "m8_a2_pass_receipt.json"
    receipt_blob = json_bytes(receipt)
    write_bytes_exclusive(local_receipt, receipt_blob)
    require_equal("resumed local receipt semantics", read_json(local_receipt),
                  receipt)
    write_bytes_exclusive(tracked_receipt, receipt_blob)
    receipt_sha = sha256_bytes(receipt_blob)
    print("m8_a2: PASS validated saved runs and published receipt", flush=True)
    return receipt_sha


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--section", choices=("uniform",), default="uniform")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--check", action="store_true",
                        help="validate saved results and historical receipt without rerunning")
    parser.add_argument("--resume-existing", action="store_true",
                        help="validate completed saved runs and publish their receipt")
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    repo = workspace.parent
    root = (args.output or workspace / "build/m8_a2_uniform").resolve()
    build_root = (workspace / "build").resolve()
    try:
        root.relative_to(build_root)
    except ValueError:
        fail(f"M8-A2 output must be below {build_root}: {root}")
    if root == build_root or not root.name.startswith("m8_a2_"):
        fail("M8-A2 output must be a child named m8_a2_* below build")
    tracked = (args.receipt.resolve() if args.receipt else
               workspace / "validation/m8_receipts/m8_a2_uniform.json")
    receipt_root = (workspace / "validation/m8_receipts").resolve()
    try:
        tracked.relative_to(receipt_root)
    except ValueError:
        fail(f"tracked receipt must be below {receipt_root}: {tracked}")
    if tracked.suffix != ".json":
        fail("tracked receipt must be a JSON file")
    validate_repository(repo, workspace)
    policy = load_calibration_policy(repo, workspace)
    if args.check and args.resume_existing:
        fail("--check and --resume-existing are mutually exclusive")
    if args.check:
        receipt_sha = check_segment(repo, workspace, root, tracked, policy)
        print("m8_a2: PASS checked historical results and tracked receipt; "
              "this check does not attest the current executable")
    elif args.resume_existing:
        receipt_sha = resume_existing_segment(repo, workspace, root, tracked,
                                              policy)
    else:
        receipt_sha = run_segment(repo, workspace, root, tracked,
                                  args.skip_build, policy)
    print(f"m8_a2: receipt={tracked}")
    print(f"m8_a2: receipt_sha256={receipt_sha}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except M8A2Failure as exc:
        print(f"m8_a2: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
