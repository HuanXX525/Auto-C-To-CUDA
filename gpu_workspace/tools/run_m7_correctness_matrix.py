#!/usr/bin/env python3
"""Run the M7 CPU/GPU correctness matrix for the production campaign.

The earlier M3--M6 scripts are intentionally focused gates.  M7 is the
cross-product gate: this runner materializes the unchanged Aircraft-Simu
schema, runs CPU and CUDA result directories, and checks semantic equality at
the required tolerance.  It also checks mapping/chunk/trace-mode invariance,
mixed-instance isolation, long (12000-step) timeout runs, and pre-launch
configuration errors.

Generated files are confined to ``gpu_workspace/build/m7_correctness`` (or
``--output``).  The runner never edits the pinned Aircraft-Simu checkout.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import math
import shutil
import struct
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, NoReturn, Sequence


RTOL = 1.0e-10
ATOL = 1.0e-12
SHORT_STEPS = 64
LONG_STEPS = 12000
ALL_CHUNK_EXTRA = 3
INSTANCE_COUNTS = (1, 2, 8, 32, 64, 128)
MAPPINGS = (
    ("serial", "serial", 1),
    ("packed-4", "packed", 4),
    ("packed-32", "packed", 32),
)
SECTION_ORDER = ("short", "mapping", "chunk", "batch", "long", "errors")
ERROR_CASES = ("missing_reference", "invalid_dt", "missing_tile_error")
LONG_CASES = ("quiet", "noise", "dropout", "timeout")
LONG_FIELD_CALIBRATION_CONTRACT = (
    ("long_quiet", "SensorFrame.missile_vel_ecef_meas.y", 1.0e-10),
)
BATCH_PROFILE_CYCLE = (
    "quiet", "noise", "dropout", "hit", "timeout",
    "fault_overlap_recovery",
)
SHORT_CASE_CONTRACT = (
    ("quiet", "profile", "quiet", "timeout", None),
    ("noise", "profile", "noise", "timeout", None),
    ("dropout", "profile", "dropout", "timeout", None),
    ("sensor_faults", "m4_case", "sensor_faults", "timeout", None),
    ("actuator_faults", "m4_case", "actuator_faults", "timeout", None),
    ("communication_delay", "m4_case", "communication_delay", "timeout", None),
    ("communication_reorder", "m4_case", "communication_reorder", "timeout", None),
    ("fault_overlap_recovery", "m4_case", "fault_overlap_recovery", "timeout", None),
    ("terrain_flyover", "m4_case", "terrain_flyover", "timeout", None),
    ("terrain_collision", "profile", "terrain_collision",
     "surface_collision", 1.0e-9),
    ("terrain_missing_tile", "m4_case", "missing_flat", "timeout", None),
    ("los_clear", "m4_case", "los_clear", "timeout", None),
    ("los_occluded", "m4_case", "los_index_occluded", "timeout", None),
    ("los_borderline", "m4_case", "los_borderline", "timeout", None),
    ("hit", "profile", "hit", "hit", None),
    ("timeout", "profile", "timeout", "timeout", None),
)


class M7Failure(RuntimeError):
    pass


def fail(message: str) -> NoReturn:
    raise M7Failure(message)


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        fail(f"missing JSON file: {path} ({exc})")
    except json.JSONDecodeError as exc:
        fail(f"invalid JSON file {path}: {exc}")


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def write_json_atomic(path: Path, value: Any) -> None:
    """Publish a complete JSON value with a same-directory atomic replace."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_fingerprint(base: Path, roots: Sequence[Path]) -> dict[str, Any]:
    """Hash file names and contents so ignored evidence remains auditable."""
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
    return {
        "sha256": digest.hexdigest(),
        "file_count": len(files),
        "total_bytes": total_bytes,
    }


def git_revision(directory: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(directory), "rev-parse", "HEAD"],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        fail(f"cannot resolve git revision for {directory}: {result.stderr.strip()}")
    return result.stdout.strip()


def git_tracked_changes(directory: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(directory), "status", "--porcelain",
         "--untracked-files=no"], text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        fail(f"cannot inspect git worktree {directory}: {result.stderr.strip()}")
    return result.stdout.strip()


def run(command: Sequence[str], cwd: Path, label: str,
        *, capture: bool = False, check: bool = True) -> subprocess.CompletedProcess[str]:
    print(f"m7: {label}")
    result = subprocess.run(
        list(command), cwd=cwd, text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
        check=False,
    )
    if check and result.returncode != 0:
        details = ""
        if capture:
            details = f"\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        fail(f"{label} failed with status {result.returncode}{details}")
    return result


def set_nested(document: dict[str, Any], dotted: str, value: Any) -> None:
    target: Any = document
    parts = dotted.split(".")
    for part in parts[:-1]:
        target = target[part]
    target[parts[-1]] = value


def validate_specification(specification: dict[str, Any], repo: Path) -> None:
    """Reject matrix-definition drift before creating acceptance results."""
    if specification.get("stage") != "M7":
        fail("matrix definition is not an M7 specification")
    if tuple(specification.get("instance_counts", ())) != INSTANCE_COUNTS:
        fail("M7 instance-count definition changed unexpectedly")
    if int(specification.get("short_steps", -1)) != SHORT_STEPS:
        fail("M7 short horizon changed unexpectedly")
    if int(specification.get("long_steps", -1)) != LONG_STEPS:
        fail("M7 long horizon changed unexpectedly")
    tolerance = specification.get("tolerance", {})
    if (float(tolerance.get("relative", -1.0)) != RTOL or
            float(tolerance.get("absolute", -1.0)) != ATOL):
        fail("M7 default tolerance changed unexpectedly")
    mappings = tuple(
        (item.get("name"), item.get("mapping"), item.get("lanes_per_block"))
        for item in specification.get("mappings", ()))
    if mappings != MAPPINGS:
        fail("M7 mapping definition changed unexpectedly")
    if tuple(specification.get("chunk_steps", ())) != (1, 64, 256, "all"):
        fail("M7 chunk definition changed unexpectedly")
    if tuple(specification.get("trace_modes", ())) != ("full", "summary"):
        fail("M7 trace-mode definition changed unexpectedly")
    if tuple(specification.get("error_cases", ())) != ERROR_CASES:
        fail("M7 error-case definition changed unexpectedly")

    cases = specification.get("short_cases", ())
    short_contract = []
    for case in cases:
        selectors = [key for key in ("profile", "m4_case") if key in case]
        if len(selectors) != 1:
            fail(f"short case {case.get('name')!r} needs one input selector")
        if "expected_exit" not in case:
            fail(f"short case {case.get('name')!r} has no expected exit")
        calibration = case.get("sensor_agl_atol")
        if calibration is not None:
            calibration = float(calibration)
            if not math.isfinite(calibration) or calibration < ATOL:
                fail(f"short case {case.get('name')!r} has invalid AGL tolerance")
        short_contract.append((
            case.get("name"), selectors[0], case.get(selectors[0]),
            case.get("expected_exit"), calibration))
    if tuple(short_contract) != SHORT_CASE_CONTRACT:
        fail("M7 short-case names or semantics changed unexpectedly")
    collision = cases[9]
    if not collision.get("tolerance_reason"):
        fail("terrain_collision field calibration needs a recorded reason")
    if tuple(specification.get("long_cases", ())) != LONG_CASES:
        fail("M7 long-case definition changed unexpectedly")
    long_calibrations = specification.get("long_field_calibrations", ())
    long_calibration_contract = tuple(
        (item.get("case"), item.get("field"), float(item.get("atol", -1.0)))
        for item in long_calibrations)
    if long_calibration_contract != LONG_FIELD_CALIBRATION_CONTRACT:
        fail("M7 long field-calibration definition changed unexpectedly")
    for calibration in long_calibrations:
        value = float(calibration["atol"])
        if (not math.isfinite(value) or value < ATOL or
                not calibration.get("reason")):
            fail("M7 long field calibration is invalid or lacks a reason")
    if tuple(specification.get("batch_profile_cycle", ())) != BATCH_PROFILE_CYCLE:
        fail("M7 batch profile cycle changed unexpectedly")

    expected_gitlink = specification.get("source_gitlink_commit")
    actual_gitlink = git_revision(repo / "external/Aircraft-Simu")
    if expected_gitlink != actual_gitlink:
        fail(f"Aircraft-Simu revision {actual_gitlink} does not match "
             f"matrix revision {expected_gitlink}")
    tracked_changes = git_tracked_changes(repo / "external/Aircraft-Simu")
    if tracked_changes:
        fail("Aircraft-Simu has tracked worktree changes; CPU baseline is not frozen")


def remove_optional_map_assets(scenario: dict[str, Any]) -> None:
    mapping = scenario["map"]
    for key in ("tile_path", "tile_paths", "tile_index_path"):
        mapping.pop(key, None)


def disable_terrain(scenario: dict[str, Any]) -> None:
    mapping = scenario["map"]
    mapping["enable_terrain"] = False
    mapping["enable_los_occlusion"] = False
    mapping["missing_tile_policy"] = "FLAT_FILL"
    remove_optional_map_assets(scenario)


def zero_sensor_noise(scenario: dict[str, Any]) -> None:
    sensors = scenario["sensors"]
    for name in ("imu", "accelerometer", "speedometer"):
        sensor = sensors[name]
        sensor["delay_s"] = 0.0
        sensor["dropout_probability"] = 0.0
        noise = sensor["noise"]
        noise["bias_xyz"] = [0.0, 0.0, 0.0]
        noise["white_noise_std"] = 0.0
        noise["random_walk_std"] = 0.0
        noise["resolution"] = 0.0
    seeker = sensors["seeker"]
    seeker["delay_s"] = 0.0
    seeker["dropout_probability"] = 0.0
    for key in ("range_noise", "los_unit_noise", "los_rate_noise",
                "closing_velocity_noise"):
        noise = seeker[key]
        for field in ("bias", "white_noise_std", "random_walk_std",
                      "resolution"):
            if field in noise:
                if isinstance(noise[field], list):
                    noise[field] = [0.0 for _ in noise[field]]
                else:
                    noise[field] = 0.0
        if "bias_xyz" in noise:
            noise["bias_xyz"] = [0.0, 0.0, 0.0]


def set_dropout(scenario: dict[str, Any], probability: float = 0.3) -> None:
    for name in ("imu", "accelerometer", "speedometer", "seeker"):
        scenario["sensors"][name]["dropout_probability"] = probability


def make_profile_scenario(base: dict[str, Any], profile: str,
                          steps: int) -> dict[str, Any]:
    scenario = copy.deepcopy(base)
    dt = float(scenario["simulation"]["dt"])
    scenario["simulation"]["max_time"] = float(steps) * dt
    if profile in ("quiet", "noise", "dropout", "hit", "timeout"):
        disable_terrain(scenario)
    if profile == "quiet":
        zero_sensor_noise(scenario)
    elif profile == "dropout":
        set_dropout(scenario)
    elif profile == "hit":
        zero_sensor_noise(scenario)
        scenario["simulation"]["hit_radius_m"] = 1.0e9
    elif profile == "timeout":
        scenario["simulation"]["hit_radius_m"] = 1.0e-9
    elif profile != "noise":
        fail(f"unknown scenario profile: {profile}")
    if profile in ("quiet", "noise", "dropout", "timeout"):
        scenario["simulation"]["hit_radius_m"] = (
            1.0e-9 if profile != "quiet" or steps >= LONG_STEPS else
            float(scenario["simulation"].get("hit_radius_m", 0.01)))
    return scenario


def make_runtime(path: Path, workspace: Path, campaign_id: str,
                 instances: list[dict[str, Any]], cpu_output: Path,
                 port: int) -> None:
    # The original CPU applications create the leaf output directory with a
    # single mkdir call; create its parent here so nested M7 case names remain
    # compatible with that unchanged behavior.
    cpu_output.parent.mkdir(parents=True, exist_ok=True)
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
            "output_dir": str(cpu_output.resolve()),
            "instance_dir_template": "instance_${instance_id}",
            "binary_logs": True,
            "event_log": True,
            "flush_every_steps": 100,
        },
        "tools": {
            "environment_program": str(
                (workspace / "build/cpu_baseline/environment_sim").resolve()),
            "flight_control_program": str(
                (workspace / "build/cpu_baseline/flight_control_sim").resolve()),
        },
        "instances": instances,
    }
    write_json(path, runtime)


def make_instance(instance_id: int, scenario: Path, faults: Path,
                  flight_control: Path, seed: int | None = None) -> dict[str, Any]:
    return {
        "enabled": True,
        "instance_id": instance_id,
        "scenario": str(scenario.resolve()),
        "flight_control": str(flight_control.resolve()),
        "faults": str(faults.resolve()),
        "random_seed": int(seed if seed is not None else 12345 + instance_id),
    }


def compile_terrain_generator(workspace: Path, root: Path) -> Path:
    """Build the tiny host tile generator without relying on an old build."""
    headers = workspace / "staging/headers"
    repo = workspace.parent
    root.mkdir(parents=True, exist_ok=True)
    map_object = root / "m7_map_tile.o"
    crc_object = root / "m7_crc32.o"
    generator = root / "m7_terrain_tile_generator"
    run(["gcc", "-std=c11", "-I", str(headers), "-c",
         str(repo / "external/Aircraft-Simu/environment_sim/src/map_tile.c"),
         "-o", str(map_object)], repo, "compile terrain map reader")
    run(["gcc", "-std=c11", "-I", str(headers), "-c",
         str(repo / "external/Aircraft-Simu/common/src/crc32.c"),
         "-o", str(crc_object)], repo, "compile terrain CRC helper")
    run(["g++", "-std=c++14", "-I", str(headers),
         str(workspace / "host_tests/terrain_tile_generator.cpp"),
         str(map_object), str(crc_object), "-lm", "-o", str(generator)],
        repo, "link terrain tile generator")
    return generator


def prepare_m4_assets(workspace: Path, root: Path) -> Path:
    generator = compile_terrain_generator(workspace, root / "m4_assets")
    run([sys.executable, str(workspace / "tools/prepare_m4_cases.py"),
         "--repo", str(workspace.parent), "--output", str(root / "m4_assets"),
         "--generator", str(generator)], workspace.parent,
        "materialize M4 terrain and fault assets")
    return generator


def create_scenario_assets(workspace: Path, root: Path,
                           base: dict[str, Any]) -> dict[str, Any]:
    scenario_dir = root / "scenarios"
    scenario_dir.mkdir(parents=True, exist_ok=True)
    faults_dir = root / "faults"
    faults_dir.mkdir(parents=True, exist_ok=True)
    empty_faults = faults_dir / "empty.json"
    write_json(empty_faults, {"schema_version": 1, "faults": []})
    baseline_fc = workspace.parent / "external/Aircraft-Simu/configs/baseline/flight_control.json"
    profiles: dict[str, Any] = {"faults": {"empty": empty_faults},
                                "flight_control": baseline_fc}
    for profile in ("quiet", "noise", "dropout", "hit", "timeout"):
        short_path = scenario_dir / f"{profile}_short.json"
        long_path = scenario_dir / f"{profile}_long.json"
        write_json(short_path, make_profile_scenario(base, profile, SHORT_STEPS))
        write_json(long_path, make_profile_scenario(base, profile, LONG_STEPS))
        profiles[profile] = {"short": short_path, "long": long_path,
                             "faults": empty_faults}

    m4_dir = root / "m4_assets"
    profiles["m4_dir"] = m4_dir
    # The M4 index-clear case is the explicit low terrain/clear LOS path.
    profiles["los_clear"] = {
        "scenario": m4_dir / "scenario_los_index_clear.json",
        "faults": m4_dir / "faults_los_index_clear.json",
    }
    profiles["los_index_occluded"] = {
        "scenario": m4_dir / "scenario_los_index_occluded.json",
        "faults": m4_dir / "faults_los_index_occluded.json",
    }
    profiles["los_borderline"] = {
        "scenario": m4_dir / "scenario_los_borderline.json",
        "faults": m4_dir / "faults_los_borderline.json",
    }
    profiles["missing_flat"] = {
        "scenario": m4_dir / "scenario_missing_flat.json",
        "faults": m4_dir / "faults_missing_flat.json",
    }
    for name in ("sensor_faults", "actuator_faults", "communication_delay",
                 "communication_reorder", "fault_overlap_recovery"):
        profiles[name] = {
            "scenario": m4_dir / f"scenario_{name}.json",
            "faults": m4_dir / f"faults_{name}.json",
        }

    # M4's generated tile set has a 998 m LOS-borderline tile.  The production
    # baseline missile has a downward ECEF component, so use a zero-height
    # fly-over tile and a calibrated 892 m collision tile.  The latter crosses
    # the production CPU/GPU trajectory at global step 33.
    tile_flyover = root / "m4_assets/terrain_flat_0.tile"
    tile_collision = root / "m4_assets/terrain_collide_892.tile"
    run([str(compile_terrain_generator(workspace, root / "m4_assets")),
         str(tile_flyover), "0.0"], workspace.parent,
        "generate M7 fly-over tile")
    run([str(compile_terrain_generator(workspace, root / "m4_assets")),
         str(tile_collision), "892.0"], workspace.parent,
        "generate M7 collision tile")
    for name, tile in (("terrain_flyover", tile_flyover),
                       ("terrain_collision", tile_collision)):
        scenario = copy.deepcopy(base)
        # Keep this gate focused on terrain/collision geometry.  Sensor-noise
        # parity is covered independently by noise/dropout cases and can
        # amplify the final collision-edge packet beyond the default numeric
        # tolerance without changing collision semantics.
        zero_sensor_noise(scenario)
        scenario["simulation"]["max_time"] = SHORT_STEPS * float(
            scenario["simulation"]["dt"])
        scenario["simulation"]["hit_radius_m"] = 0.01
        scenario["map"]["enable_terrain"] = True
        scenario["map"]["enable_los_occlusion"] = False
        scenario["map"]["missing_tile_policy"] = "ERROR"
        remove_optional_map_assets(scenario)
        # The pinned CPU env_app's singular map.tile_path parser increments
        # its path count without constructing the parallel cache-index entry.
        # map.tile_paths is the equivalent unchanged-schema form that builds
        # both records and is also consumed by the GPU loader.
        scenario["map"]["tile_paths"] = [str(tile.resolve())]
        scenario_path = scenario_dir / f"{name}.json"
        write_json(scenario_path, scenario)
        profiles[name] = {"scenario": scenario_path, "faults": empty_faults}
    # A separate ERROR policy with no tile must fail during initialization.
    missing_error = copy.deepcopy(base)
    missing_error["simulation"]["max_time"] = SHORT_STEPS * float(
        missing_error["simulation"]["dt"])
    missing_error["map"]["enable_terrain"] = True
    missing_error["map"]["enable_los_occlusion"] = True
    missing_error["map"]["missing_tile_policy"] = "ERROR"
    remove_optional_map_assets(missing_error)
    missing_error_path = scenario_dir / "missing_error.json"
    write_json(missing_error_path, missing_error)
    profiles["missing_error"] = {"scenario": missing_error_path,
                                  "faults": empty_faults}
    return profiles


def scenario_for(profiles: dict[str, Any], name: str, horizon: str = "short") -> Path:
    value = profiles[name]
    if isinstance(value, dict) and "scenario" in value:
        return Path(value["scenario"])
    if isinstance(value, dict) and horizon in value:
        return Path(value[horizon])
    fail(f"no scenario for profile {name}/{horizon}")


def faults_for(profiles: dict[str, Any], name: str) -> Path:
    value = profiles[name]
    if isinstance(value, dict) and "faults" in value:
        return Path(value["faults"])
    fail(f"no fault file for profile {name}")


def make_case_runtime(workspace: Path, root: Path, profiles: dict[str, Any],
                      name: str, horizon: str, count: int,
                      *, campaign_id: str, port: int,
                      profile_cycle: Sequence[str] | None = None) -> Path:
    runtime_path = root / "runtimes" / f"{campaign_id}.json"
    cpu_output = root / "cpu" / campaign_id
    baseline_fc = Path(profiles["flight_control"])
    instances: list[dict[str, Any]] = []
    for instance_id in range(count):
        profile = (profile_cycle[instance_id % len(profile_cycle)]
                   if profile_cycle else name)
        instance_horizon = horizon
        if profile in ("quiet", "noise", "dropout", "hit", "timeout"):
            scenario = scenario_for(profiles, profile, instance_horizon)
        else:
            scenario = scenario_for(profiles, profile)
        faults = faults_for(profiles, profile)
        instances.append(make_instance(instance_id, scenario, faults,
                                       baseline_fc, 12345 + instance_id * 17))
    make_runtime(runtime_path, workspace, campaign_id, instances, cpu_output,
                 port)
    return runtime_path


def run_cpu(workspace: Path, runtime: Path, label: str) -> Path:
    runtime_data = read_json(runtime)
    output = Path(runtime_data["logging"]["output_dir"])
    if output.exists():
        shutil.rmtree(output)
    result = run([str(workspace / "build/cpu_baseline/instance_manager"),
                  "--runtime", str(runtime)], workspace.parent,
                 f"CPU {label}", capture=True)
    if result.stdout:
        print("m7: " + result.stdout.strip().splitlines()[-1])
    if not output.is_dir():
        fail(f"CPU {label}: output directory was not created: {output}")
    return output


def execution_metadata(output: Path) -> dict[str, Any]:
    return read_json(output / "gpu_execution.json")


def run_gpu(workspace: Path, runtime: Path, output: Path, label: str,
            mode: str, chunk: int, mapping: str, lanes: int,
            expected_returncodes: Sequence[int] = (0,)) -> Path:
    if output.exists():
        shutil.rmtree(output)
    # The public CLI keeps one lanes option domain (4/8/16/32) for every
    # mapping.  Serial ignores that requested value and records its effective
    # single lane in gpu_execution.json.
    cli_lanes = 4 if mapping == "serial" else lanes
    result = run(
        [str(workspace / "build/bin/gpu_campaign"), "--config", str(runtime),
         "--output-dir", str(output), "--trace-mode", mode,
         "--chunk-steps", str(chunk), "--mapping", mapping,
         "--lanes-per-block", str(cli_lanes)], Path("/tmp"),
        f"GPU {label} {mode} {mapping}/{lanes} chunk={chunk}", capture=True,
        check=False)
    if result.returncode not in expected_returncodes:
        fail(f"GPU {label} {mode} {mapping}/{lanes} chunk={chunk} returned "
             f"{result.returncode}, expected one of {tuple(expected_returncodes)}"
             f"\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    if result.stdout:
        print("m7: " + result.stdout.strip().splitlines()[-1])
    metadata = execution_metadata(output)
    expected_mapping = mapping
    expected_lanes = 1 if mapping == "serial" else lanes
    if metadata.get("trace_mode") != mode:
        fail(f"{label}: trace mode metadata {metadata.get('trace_mode')!r}")
    if metadata.get("mapping") != expected_mapping:
        fail(f"{label}: mapping metadata {metadata.get('mapping')!r}")
    if metadata.get("lanes_per_block") != expected_lanes:
        fail(f"{label}: lanes metadata {metadata.get('lanes_per_block')!r}")
    if metadata.get("chunk_steps") != chunk:
        fail(f"{label}: chunk metadata {metadata.get('chunk_steps')!r}")
    return output


def compare_runs(repo: Path, left: Path, right: Path, label: str,
                 *, sensor_agl_atol: float | None = None,
                 sensor_velocity_y_atol: float | None = None) -> None:
    command = [sys.executable, str(repo / "compare_runs.py"), str(left),
               str(right), "--rtol", str(RTOL), "--atol", str(ATOL)]
    if sensor_agl_atol is not None:
        command.extend(("--sensor-agl-atol", str(sensor_agl_atol)))
    if sensor_velocity_y_atol is not None:
        command.extend(("--sensor-velocity-y-atol",
                        str(sensor_velocity_y_atol)))
    result = run(command,
                 repo, f"compare {label}", capture=True, check=False)
    if result.returncode != 0:
        fail(f"compare {label} failed:\n{result.stdout}\n{result.stderr}")


def instance_names(output: Path) -> list[str]:
    return sorted(p.name for p in output.glob("instance_[0-9][0-9][0-9][0-9]")
                  if p.is_dir())


def assert_result(output: Path, expected_exit: str | None = None,
                  expected_steps: int | None = None) -> None:
    names = instance_names(output)
    if not names:
        fail(f"{output}: no instance output")
    for name in names:
        summary = read_json(output / name / "summary.json")
        if expected_exit is not None and summary.get("exit_reason") != expected_exit:
            fail(f"{output}/{name}: exit_reason={summary.get('exit_reason')!r}, "
                 f"expected {expected_exit!r}")
        if expected_steps is not None and summary.get("simulation_steps") != expected_steps:
            fail(f"{output}/{name}: simulation_steps={summary.get('simulation_steps')!r}, "
                 f"expected {expected_steps}")


def assert_full_trace_steps(output: Path, expected_steps: int) -> None:
    for name in instance_names(output):
        path = output / name / "trajectory.csv"
        if not path.exists():
            fail(f"{path}: full trace is missing")
        rows = path.read_text(encoding="utf-8").splitlines()
        if len(rows) != expected_steps + 2:
            # The writer has one initial row plus one row per completed step;
            # its header makes the total expected_steps+2.
            fail(f"{path}: {len(rows)-1} data rows, expected {expected_steps+1}")
        # Production CSV uses simulation time (not the internal global-step
        # column) as its first field.  The row count proves every step was
        # emitted; the final time additionally catches a truncated chunk.
        last_time = float(rows[-1].split(",", 1)[0])
        if not math.isclose(last_time, expected_steps * 0.01,
                            rel_tol=0.0, abs_tol=5.0e-7):
            fail(f"{path}: last simulation time {last_time}, expected "
                 f"{expected_steps * 0.01}")


def packet_flags(output: Path) -> list[tuple[int, int]]:
    path = output / "instance_0000/sensor_log.bin"
    data = path.read_bytes()
    packet_size = 232
    if len(data) % packet_size != 0:
        fail(f"invalid sensor packet size: {path}")
    return [(struct.unpack_from("<I", data, offset + 224)[0],
             struct.unpack_from("<I", data, offset + 228)[0])
            for offset in range(0, len(data), packet_size)]


def assert_los_semantics(case: str, output: Path) -> None:
    flags = packet_flags(output)
    if not flags:
        fail(f"{case}: no sensor packets")
    occluded = [fault & 1 for _, fault in flags]
    if case == "los_clear" and any(occluded):
        fail("los_clear: unexpected LOS occlusion flag")
    if case == "los_occluded" and not all(occluded):
        fail("los_occluded: not every packet is occluded")
    if case == "los_borderline" and (not any(occluded) or all(occluded)):
        fail("los_borderline: boundary did not produce both sides")


def import_compare_module(repo: Path) -> Any:
    spec = importlib.util.spec_from_file_location("m7_compare_runs",
                                                  repo / "compare_runs.py")
    if spec is None or spec.loader is None:
        fail("cannot load compare_runs.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def compare_instance_payload(compare_module: Any, left: Path, right: Path,
                             instance_id: int, label: str) -> None:
    ok, diffs = compare_module.compare_instance(
        left, right, instance_id, RTOL, ATOL, True)
    if not ok:
        fail(f"{label}: instance payload differs:\n" + "\n".join(diffs))


def run_short_cases(workspace: Path, repo: Path, root: Path,
                    profiles: dict[str, Any], specification: dict[str, Any]) -> dict[str, Path]:
    """Run every functional short case against the CPU baseline."""
    references: dict[str, Path] = {}
    case_specs = {item["name"]: item for item in specification["short_cases"]}
    for case_name, case in case_specs.items():
        input_name = case.get("profile", case.get("m4_case"))
        runtime = make_case_runtime(
            workspace, root, profiles, input_name, "short", 1,
            campaign_id=f"short_{case_name}", port=60000 + len(references) * 4)
        cpu = run_cpu(workspace, runtime, f"short {case_name}")
        gpu = run_gpu(workspace, runtime,
                      root / "gpu" / f"short_{case_name}",
                      f"short {case_name}", "full", SHORT_STEPS,
                      "packed", 4)
        compare_runs(
            repo, cpu, gpu, f"short {case_name} CPU/GPU",
            sensor_agl_atol=(float(case["sensor_agl_atol"])
                             if "sensor_agl_atol" in case else None))
        assert_result(gpu, case["expected_exit"],
                      0 if case["expected_exit"] == "hit" else
                      (33 if case["expected_exit"] == "surface_collision" else SHORT_STEPS))
        if case_name in ("los_clear", "los_occluded", "los_borderline"):
            assert_los_semantics(case_name, gpu)
        if case_name == "terrain_collision":
            # The calibrated 892 m tile must terminate at the known M1 step.
            assert_result(gpu, "surface_collision", 33)
        references[case_name] = gpu
    return references


def run_mapping_invariance(workspace: Path, repo: Path, root: Path,
                           profiles: dict[str, Any],
                           compare_module: Any) -> None:
    """Check all required mappings at N=1 and N=128."""
    for count in (1, 128):
        runtime = make_case_runtime(
            workspace, root, profiles, "fault_overlap_recovery", "short", count,
            campaign_id=f"mapping_n{count}", port=61000 + count)
        cpu = run_cpu(workspace, runtime, f"mapping reference N={count}")
        outputs: dict[str, Path] = {}
        for name, mapping, lanes in MAPPINGS:
            output = run_gpu(workspace, runtime,
                             root / "gpu" / f"mapping_n{count}_{name}",
                             f"mapping N={count} {name}", "full", SHORT_STEPS,
                             mapping, lanes)
            compare_runs(repo, cpu, output,
                         f"mapping N={count} {name} CPU/GPU")
            outputs[name] = output
        canonical = outputs["packed-4"]
        for name, output in outputs.items():
            compare_runs(repo, canonical, output,
                         f"mapping N={count} packed-4/{name}")


def run_chunk_invariance(workspace: Path, repo: Path, root: Path,
                         profiles: dict[str, Any], specification: dict[str, Any]) -> None:
    """Exercise full/summary and every required chunk size on a stateful case."""
    runtime = make_case_runtime(
        workspace, root, profiles, "fault_overlap_recovery", "short", 1,
        campaign_id="chunk_reference", port=62000)
    cpu = run_cpu(workspace, runtime, "chunk reference")
    all_chunk = SHORT_STEPS + ALL_CHUNK_EXTRA
    chunks = tuple(
        all_chunk if value == "all" else int(value)
        for value in specification["chunk_steps"])
    full: dict[int, Path] = {}
    for chunk in chunks:
        output = run_gpu(workspace, runtime,
                         root / "gpu" / f"chunk_full_{chunk}",
                         f"chunk full={chunk}", "full", chunk, "packed", 4)
        compare_runs(repo, cpu, output, f"chunk full={chunk} CPU/GPU")
        full[chunk] = output
    canonical = full[all_chunk]
    for chunk, output in full.items():
        compare_runs(repo, canonical, output, f"chunk full={chunk} canonical")
    for chunk in chunks:
        output = run_gpu(workspace, runtime,
                         root / "gpu" / f"chunk_summary_{chunk}",
                         f"chunk summary={chunk}", "summary", chunk,
                         "packed", 4)
        compare_runs(repo, canonical, output, f"chunk summary={chunk}")
    repeat = run_gpu(workspace, runtime,
                     root / "gpu" / "chunk_full_64_repeat",
                     "chunk deterministic repeat", "full", 64,
                     "packed", 4)
    compare_runs(repo, full[64], repeat, "chunk deterministic repeat")


def run_batch_matrix(workspace: Path, repo: Path, root: Path,
                     profiles: dict[str, Any], specification: dict[str, Any],
                     compare_module: Any) -> None:
    """Run all N values with heterogeneous profiles and termination times."""
    cycle = tuple(specification["batch_profile_cycle"])
    single_refs: dict[tuple[str, int], Path] = {}
    # Independent references are only needed for one representative of each
    # profile; batch CPU/GPU comparisons cover every instance and N.
    for index, profile in enumerate(cycle):
        runtime = make_case_runtime(
            workspace, root, profiles, profile, "short", 1,
            campaign_id=f"isolation_ref_{profile}_{index}",
            port=63000 + index * 4)
        # Preserve the profile's eventual batch ID so its seed/initial state
        # is identical to the corresponding batch lane.
        data = read_json(runtime)
        data["instances"][0]["instance_id"] = index
        data["instances"][0]["random_seed"] = 12345 + index * 17
        data["campaign"]["campaign_id"] = f"isolation_ref_{profile}_{index}"
        write_json(runtime, data)
        run_cpu(workspace, runtime, f"isolation reference CPU {profile} id={index}")
        single_refs[(profile, index)] = run_gpu(
            workspace, runtime, root / "gpu" / f"isolation_ref_{profile}_{index}",
            f"isolation reference GPU {profile} id={index}", "full", SHORT_STEPS,
            "packed", 4)

    for count in INSTANCE_COUNTS:
        runtime = make_case_runtime(
            workspace, root, profiles, "quiet", "short", count,
            campaign_id=f"batch_n{count}", port=64000 + count,
            profile_cycle=cycle)
        cpu = run_cpu(workspace, runtime, f"mixed batch CPU N={count}")
        gpu = run_gpu(workspace, runtime, root / "gpu" / f"batch_n{count}",
                      f"mixed batch GPU N={count}", "full", SHORT_STEPS,
                      "packed", 4)
        compare_runs(repo, cpu, gpu, f"mixed batch N={count} CPU/GPU")
        if instance_names(gpu) != [f"instance_{i:04d}" for i in range(count)]:
            fail(f"mixed batch N={count}: instance set is not contiguous")
        # Compare each of the first cycle lanes to its independent instance
        # reference.  This catches state/RNG/fault contamination that a
        # campaign-level comparison alone could hide.
        for instance_id, profile in enumerate(cycle):
            if instance_id >= count:
                break
            compare_instance_payload(
                compare_module, single_refs[(profile, instance_id)], gpu,
                instance_id, f"mixed batch N={count} instance={instance_id}")


def run_long_matrix(workspace: Path, repo: Path, root: Path,
                    profiles: dict[str, Any], specification: dict[str, Any]) -> None:
    """Run the explicit 12000-step timeout horizon.

    Summary output is used for all four long profiles to keep the matrix
    bounded; quiet additionally emits a full 12000-step trace and is compared
    against the CPU full directory.  This proves both the long state machine
    and the full trace path without multiplying multi-gigabyte batch output.
    """
    calibrations = {
        (item["case"], item["field"]): float(item["atol"])
        for item in specification["long_field_calibrations"]
    }
    quiet_velocity_y_atol = calibrations[
        ("long_quiet", "SensorFrame.missile_vel_ecef_meas.y")]
    for index, profile in enumerate(specification["long_cases"]):
        runtime = make_case_runtime(
            workspace, root, profiles, profile, "long", 1,
            campaign_id=f"long_{profile}", port=65000 + index * 4)
        cpu = run_cpu(workspace, runtime, f"long {profile} 12000-step")
        gpu_summary = run_gpu(
            workspace, runtime, root / "gpu" / f"long_{profile}_summary",
            f"long {profile} summary", "summary", 256, "packed", 4)
        compare_runs(repo, cpu, gpu_summary,
                     f"long {profile} CPU/summary")
        assert_result(gpu_summary, "timeout", LONG_STEPS)
        if profile == "quiet":
            gpu_full = run_gpu(
                workspace, runtime, root / "gpu" / "long_quiet_full",
                "long quiet full", "full", 256, "packed", 4)
            compare_runs(
                repo, cpu, gpu_full, "long quiet CPU/full",
                sensor_velocity_y_atol=quiet_velocity_y_atol)
            assert_result(gpu_full, "timeout", LONG_STEPS)
            assert_full_trace_steps(gpu_full, LONG_STEPS)
            compare_runs(repo, gpu_full, gpu_summary,
                         "long quiet full/summary")


def run_error_matrix(workspace: Path, root: Path, profiles: dict[str, Any]) -> None:
    """Verify invalid references/ranges fail before a kernel is launched."""
    errors = root / "errors"
    errors.mkdir(parents=True, exist_ok=True)
    base_runtime = make_case_runtime(
        workspace, root, profiles, "quiet", "short", 1,
        campaign_id="error_base", port=65100)
    base = read_json(base_runtime)
    # Missing per-instance reference.
    missing = copy.deepcopy(base)
    missing["campaign"]["campaign_id"] = "missing_reference"
    missing["instances"][0]["scenario"] = str(errors / "does_not_exist.json")
    missing_path = errors / "missing_reference.json"
    write_json(missing_path, missing)
    missing_out = errors / "missing_reference_output"
    result = run([str(workspace / "build/bin/gpu_campaign"), "--config",
                  str(missing_path), "--output-dir", str(missing_out)],
                 Path("/tmp"), "error missing reference", capture=True, check=False)
    if result.returncode == 0 or missing_out.exists():
        fail("missing reference was accepted or created a launch output")

    # Invalid numeric timing must be rejected by host mapping.
    invalid_scenario = errors / "invalid_dt.json"
    scenario = read_json(scenario_for(profiles, "quiet", "short"))
    scenario["simulation"]["dt"] = 0.0
    write_json(invalid_scenario, scenario)
    invalid = copy.deepcopy(base)
    invalid["campaign"]["campaign_id"] = "invalid_dt"
    invalid["instances"][0]["scenario"] = str(invalid_scenario)
    invalid_path = errors / "invalid_dt.json.runtime"
    write_json(invalid_path, invalid)
    invalid_out = errors / "invalid_dt_output"
    result = run([str(workspace / "build/bin/gpu_campaign"), "--config",
                  str(invalid_path), "--output-dir", str(invalid_out)],
                 Path("/tmp"), "error invalid dt", capture=True, check=False)
    if result.returncode == 0 or invalid_out.exists():
        fail("invalid dt was accepted or created a launch output")

    # Missing terrain with ERROR is a valid campaign-level error result, but
    # must terminate before step 1 and be represented in CPU-compatible data.
    missing_tile_runtime = make_case_runtime(
        workspace, root, profiles, "missing_error", "short", 1,
        campaign_id="missing_tile_error", port=65120)
    output = run_gpu(workspace, missing_tile_runtime,
                     errors / "missing_tile_error_output",
                     "error missing tile", "summary", 64, "packed", 4,
                     expected_returncodes=(1,))
    assert_result(output, "terrain_initialization_failed", 0)


def write_pass_receipt(workspace: Path, repo: Path, root: Path,
                       sections: Sequence[str],
                       tracked_receipt: Path | None = None) -> list[Path]:
    """Write the segment PASS marker only after every requested gate passed."""
    matrix_path = workspace / "configs/m7/cases.json"
    comparator_path = repo / "compare_runs.py"
    runner_path = Path(__file__).resolve()
    input_paths = {
        "runner": runner_path,
        "matrix": matrix_path,
        "comparator": comparator_path,
        "m4_asset_generator": workspace / "tools/prepare_m4_cases.py",
        "m4_case_matrix": workspace / "configs/m4/cases.json",
        "terrain_tile_generator": workspace / "host_tests/terrain_tile_generator.cpp",
        "cpu_build_script": workspace / "tools/build_cpu_baseline.sh",
        "gpu_makefile": workspace / "Makefile",
        "baseline_scenario": repo / "external/Aircraft-Simu/configs/baseline/scenario.json",
        "baseline_flight_control": repo / "external/Aircraft-Simu/configs/baseline/flight_control.json",
    }
    binary_candidates = {
        "gpu_campaign": workspace / "build/bin/gpu_campaign",
        "cpu_environment": workspace / "build/cpu_baseline/environment_sim",
        "cpu_flight_control": workspace / "build/cpu_baseline/flight_control_sim",
        "cpu_instance_manager": workspace / "build/cpu_baseline/instance_manager",
    }
    execution_paths = sorted(root.rglob("gpu_execution.json"))
    executions = [read_json(path) for path in execution_paths]
    matrix = read_json(matrix_path)
    hardware = sorted({item.get("gpu") for item in executions
                       if item.get("gpu")})
    cuda_versions = sorted({item.get("cuda_runtime_version") for item in executions
                            if item.get("cuda_runtime_version") is not None})
    receipt = {
        "schema_version": 1,
        "phase": "M7",
        "status": "PASS",
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "sections": list(sections),
        "complete_m7_gate": tuple(sections) == SECTION_ORDER,
        "command": [sys.executable, str(runner_path), *sys.argv[1:]],
        "source": {
            "parent_commit": git_revision(repo),
            "aircraft_simu_commit": git_revision(repo / "external/Aircraft-Simu"),
            "aircraft_simu_tracked_worktree_clean": not bool(
                git_tracked_changes(repo / "external/Aircraft-Simu")),
            "sha256": {name: sha256_file(path)
                       for name, path in input_paths.items()},
            "binary_sha256": {
                name: sha256_file(path)
                for name, path in binary_candidates.items() if path.is_file()
            },
        },
        "tolerance": {
            "rtol": RTOL,
            "atol": ATOL,
            "field_calibrations": [
                {
                    "case": case["name"],
                    "field": "SensorFrame.missile_height_agl_m_meas",
                    "atol": case["sensor_agl_atol"],
                    "reason": case["tolerance_reason"],
                }
                for case in matrix["short_cases"]
                if "sensor_agl_atol" in case
            ] + [
                {
                    "case": calibration["case"],
                    "field": calibration["field"],
                    "atol": calibration["atol"],
                    "reason": calibration["reason"],
                }
                for calibration in matrix["long_field_calibrations"]
            ],
        },
        "hardware": hardware,
        "cuda_runtime_versions": cuda_versions,
        "artifacts": {
            "gpu_execution_count": len(execution_paths),
            "cpu_campaign_count": len(list((root / "cpu").glob(
                "*/campaign_summary.json"))),
            "gpu_campaign_count": len(list((root / "gpu").glob(
                "*/campaign_summary.json"))),
            "generated_inputs": tree_fingerprint(
                root, [root / "runtimes", root / "scenarios",
                       root / "faults", root / "m4_assets"]),
            "results": tree_fingerprint(
                root, [root / "cpu", root / "gpu", root / "errors"]),
        },
    }
    receipt_path = root / "m7_pass_receipt.json"
    if tracked_receipt is not None:
        write_json_atomic(tracked_receipt, receipt)
    write_json_atomic(receipt_path, receipt)
    receipts = [receipt_path]
    if tracked_receipt is not None:
        receipts.append(tracked_receipt)
    return receipts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--receipt", type=Path,
        help=("also publish the PASS receipt below "
              "gpu_workspace/validation/m7_receipts for version control"))
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-cpu", action="store_true",
                        help="skip CPU runs and CPU/GPU comparisons")
    parser.add_argument(
        "--sections", default="short,mapping,chunk,batch,long,errors",
        help=("comma-separated sections for focused diagnosis; the default "
              "runs the complete M7 gate"))
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    repo = workspace.parent
    root = (args.output or workspace / "build/m7_correctness").resolve()
    build_root = (workspace / "build").resolve()
    try:
        root.relative_to(build_root)
    except ValueError:
        fail(f"M7 output must be below {build_root}: {root}")
    if root == build_root:
        fail("M7 output must not replace the workspace build directory")
    if not root.name.startswith("m7_"):
        fail("M7 output directory name must start with 'm7_'")
    tracked_receipt = args.receipt.resolve() if args.receipt else None
    if tracked_receipt is not None:
        receipt_root = (workspace / "validation/m7_receipts").resolve()
        try:
            tracked_receipt.relative_to(receipt_root)
        except ValueError:
            fail(f"tracked receipt must be below {receipt_root}: "
                 f"{tracked_receipt}")
        if tracked_receipt == receipt_root or tracked_receipt.suffix != ".json":
            fail("tracked receipt must be a JSON file below validation/m7_receipts")
        if tracked_receipt.exists():
            fail(f"tracked receipt already exists; explicitly invalidate it "
                 f"before rerunning: {tracked_receipt}")
    if args.skip_cpu:
        fail("M7 correctness acceptance cannot claim PASS with --skip-cpu")

    requested_sections = [item.strip() for item in args.sections.split(",")
                          if item.strip()]
    sections = set(requested_sections)
    valid_sections = set(SECTION_ORDER)
    unknown = sections - valid_sections
    if not sections or unknown or len(sections) != len(requested_sections):
        fail(f"invalid --sections value; unknown={sorted(unknown)}")
    ordered_sections = tuple(item for item in SECTION_ORDER if item in sections)

    specification = read_json(workspace / "configs/m7/cases.json")
    validate_specification(specification, repo)
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)

    if not args.skip_build:
        run([str(workspace / "tools/build_cpu_baseline.sh")], repo,
            "build CPU baseline")
        run(["make", "-C", str(workspace), "-j2"], repo,
            "build GPU campaign")

    base = read_json(repo / "external/Aircraft-Simu/configs/baseline/scenario.json")
    prepare_m4_assets(workspace, root)
    profiles = create_scenario_assets(workspace, root, base)
    compare_module = import_compare_module(repo)

    if "short" in sections:
        run_short_cases(workspace, repo, root, profiles, specification)
    if "mapping" in sections:
        run_mapping_invariance(workspace, repo, root, profiles, compare_module)
    if "chunk" in sections:
        run_chunk_invariance(workspace, repo, root, profiles, specification)
    if "batch" in sections:
        run_batch_matrix(workspace, repo, root, profiles, specification, compare_module)
    if "long" in sections:
        run_long_matrix(workspace, repo, root, profiles, specification)
    if "errors" in sections:
        run_error_matrix(workspace, root, profiles)

    receipts = write_pass_receipt(workspace, repo, root, ordered_sections,
                                  tracked_receipt)
    if sections == valid_sections:
        print("m7_correctness_matrix: PASS short functional cases, "
              "N=1/2/8/32/64/128, serial/packed-4/packed-32, "
              "chunk=1/64/256/all, full/summary, mixed isolation, "
              "12000-step CPU/GPU regression, and pre-launch errors")
    else:
        print("m7_correctness_matrix: PASS focused sections=" +
              ",".join(ordered_sections))
    for receipt in receipts:
        print(f"m7_correctness_matrix: receipt={receipt}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except M7Failure as exc:
        print(f"m7_correctness_matrix: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
