#!/usr/bin/env python3
"""Materialize and validate the frozen M8-A1 long multi-instance inputs.

The generated configuration tree is tracked, while compiler products and the
local copy of the PASS receipt stay below ``gpu_workspace/build``.  This tool
never deletes or replaces an existing result.  ``--write`` publishes a new
tree and byte-identical local/tracked receipts; ``--check`` regenerates the
expected bytes in memory and verifies the published evidence.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, NoReturn, Sequence


AIRCRAFT_COMMIT = "970bdfbe97db21aafb6cd94afee0ecbe72c8544c"
M7_REPORT_SHA256 = "6068d5bb44c3fdc4bde11ea05aabfc7cffbde8a165db1f88bce4450ac2b90627"
M8_START_PARENT = "9761dcecdc4402c9fa5913abb84b72053bc01128"
PROFILE_CYCLE = (
    "quiet",
    "noise",
    "dropout",
    "sensor_faults",
    "actuator_faults",
    "communication_delay",
    "communication_reorder",
    "fault_overlap_recovery",
)
FAULT_PROFILES = (
    "sensor_faults",
    "actuator_faults",
    "communication_delay",
    "communication_reorder",
    "fault_overlap_recovery",
)
RUNTIME_NAMES = ("uniform_n64", "uniform_n128", "heterogeneous_n128")


class M8Failure(RuntimeError):
    pass


def fail(message: str) -> NoReturn:
    raise M8Failure(message)


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
    try:
        stream = path.open("rb")
    except FileNotFoundError as exc:
        fail(f"required file is missing: {path} ({exc})")
    digest = hashlib.sha256()
    with stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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


def disable_terrain(scenario: dict[str, Any]) -> None:
    mapping = scenario["map"]
    mapping["enable_terrain"] = False
    mapping["enable_los_occlusion"] = False
    mapping["missing_tile_policy"] = "FLAT_FILL"
    for key in ("tile_path", "tile_paths", "tile_index_path"):
        mapping.pop(key, None)


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
        if "bias" in noise:
            noise["bias"] = 0.0
        if "bias_xyz" in noise:
            noise["bias_xyz"] = [0.0, 0.0, 0.0]
        noise["white_noise_std"] = 0.0
        noise["random_walk_std"] = 0.0
        noise["resolution"] = 0.0


def set_dropout(scenario: dict[str, Any], probability: float) -> None:
    for name in ("imu", "accelerometer", "speedometer", "seeker"):
        scenario["sensors"][name]["dropout_probability"] = probability


def make_long_scenario(base: dict[str, Any], specification: dict[str, Any]) -> dict[str, Any]:
    scenario = copy.deepcopy(base)
    horizon = specification["long_horizon"]
    scenario["simulation"]["dt"] = float(horizon["dt_s"])
    scenario["simulation"]["max_time"] = float(horizon["max_time_s"])
    scenario["simulation"]["hit_radius_m"] = float(horizon["hit_radius_m"])
    disable_terrain(scenario)
    return scenario


def perturb_scenario(scenario: dict[str, Any], instance_id: int,
                     specification: dict[str, Any]) -> None:
    perturbation = specification["datasets"]["heterogeneous"]["perturbation"]
    row = instance_id // 16
    column = instance_id % 16
    missile_lla = scenario["missile"]["initial_lla_deg_m"]
    missile_velocity = scenario["missile"]["initial_velocity_ecef_mps"]
    target_lla = scenario["target"]["initial_lla_deg_m"]
    target_velocity = scenario["target"]["initial_velocity_ecef_mps"]

    missile_lla[0] += (row - 3.5) * float(
        perturbation["missile_latitude_deg_per_row"])
    missile_lla[1] += (column - 7.5) * float(
        perturbation["missile_longitude_deg_per_column"])
    missile_heights = perturbation["missile_height_m_cycle"]
    missile_lla[2] += float(missile_heights[instance_id % len(missile_heights)])
    missile_velocity[0] += (instance_id % 9 - 4) * float(
        perturbation["missile_velocity_x_mps_cycle_step"])
    missile_velocity[1] += (instance_id % 11 - 5) * float(
        perturbation["missile_velocity_y_mps_cycle_step"])
    missile_velocity[2] += (instance_id % 5 - 2) * float(
        perturbation["missile_velocity_z_mps_cycle_step"])

    target_lla[0] += (column % 8 - 3.5) * float(
        perturbation["target_latitude_deg_per_column_mod8"])
    target_lla[1] += (row - 3.5) * float(
        perturbation["target_longitude_deg_per_row"])
    target_heights = perturbation["target_height_m_cycle"]
    target_lla[2] += float(target_heights[instance_id % len(target_heights)])
    target_velocity[0] += (instance_id % 7 - 3) * float(
        perturbation["target_velocity_x_mps_cycle_step"])
    target_velocity[1] += (instance_id % 9 - 4) * float(
        perturbation["target_velocity_y_mps_cycle_step"])
    target_velocity[2] += (instance_id % 3 - 1) * float(
        perturbation["target_velocity_z_mps_cycle_step"])


def make_instance(instance_id: int, scenario: str, faults: str,
                  specification: dict[str, Any]) -> dict[str, Any]:
    instances = specification["instances"]
    return {
        "enabled": True,
        "instance_id": instance_id,
        "scenario": scenario,
        "flight_control": specification["source"]["baseline_flight_control"],
        "faults": faults,
        "random_seed": int(instances["seed_base"]) +
                       int(instances["seed_stride"]) * instance_id,
    }


def make_runtime(source: dict[str, Any], specification: dict[str, Any],
                 campaign_id: str, count: int, port: int,
                 scenario_paths: Sequence[str], fault_paths: Sequence[str]) -> dict[str, Any]:
    if len(scenario_paths) != count or len(fault_paths) != count:
        fail(f"{campaign_id}: instance path count mismatch")
    network = copy.deepcopy(source["network"])
    network["environment_base_port"] = port
    network["flight_control_base_port"] = port + 1
    return {
        "schema_version": 1,
        "campaign": {
            "campaign_id": campaign_id,
            "instance_count": count,
            "schedule": "PARALLEL",
            "max_parallel_instances": int(
                specification["instances"]["cpu_parallelism"]),
            "base_random_seed": int(specification["instances"]["seed_base"]),
            "failure_strategy": "CONTINUE_ON_FAILURE",
        },
        "network": network,
        "logging": {
            "output_dir": f"gpu_workspace/build/m8_runs/{campaign_id}",
            "instance_dir_template": "instance_${instance_id}",
            "binary_logs": True,
            "event_log": True,
            "flush_every_steps": 100,
        },
        "tools": {
            "environment_program": "gpu_workspace/build/cpu_baseline/environment_sim",
            "flight_control_program": "gpu_workspace/build/cpu_baseline/flight_control_sim",
        },
        "instances": [
            make_instance(index, scenario_paths[index], fault_paths[index],
                          specification)
            for index in range(count)
        ],
    }


def validate_specification(specification: dict[str, Any], repo: Path) -> None:
    require_equal("M8 schema", specification.get("schema_version"), 1)
    require_equal("M8 phase", specification.get("phase"), "M8")
    require_equal("M8 segment", specification.get("segment"), "M8-A1")
    source = specification["source"]
    source_paths = {
        "m7_closure_report": "gpu_workspace/validation/correctness_m7.json",
        "baseline_runtime": "external/Aircraft-Simu/configs/baseline/runtime.json",
        "baseline_scenario": "external/Aircraft-Simu/configs/baseline/scenario.json",
        "baseline_flight_control":
            "external/Aircraft-Simu/configs/baseline/flight_control.json",
        "validated_fault_matrix": "gpu_workspace/configs/m4/cases.json",
    }
    for key, value in source_paths.items():
        require_equal(f"source path {key}", source.get(key), value)
    require_equal("M8 start parent", source.get("m8_start_parent_commit"),
                  M8_START_PARENT)
    require_equal("Aircraft-Simu contract", source.get("aircraft_simu_commit"),
                  AIRCRAFT_COMMIT)
    require_equal("M7 closure contract", source.get("m7_closure_sha256"),
                  M7_REPORT_SHA256)
    require_equal("M7 closure content",
                  sha256_file(repo / source["m7_closure_report"]),
                  M7_REPORT_SHA256)
    actual_aircraft = git(repo / "external/Aircraft-Simu", ["rev-parse", "HEAD"],
                          "resolve Aircraft-Simu revision")
    require_equal("Aircraft-Simu revision", actual_aircraft, AIRCRAFT_COMMIT)
    changes = git(repo / "external/Aircraft-Simu",
                  ["status", "--porcelain", "--untracked-files=no"],
                  "inspect Aircraft-Simu worktree")
    require_equal("Aircraft-Simu tracked worktree", changes, "")
    current_parent = git(repo, ["rev-parse", "HEAD"], "resolve parent revision")
    ancestry = subprocess.run(
        ["git", "-C", str(repo), "merge-base", "--is-ancestor",
         M8_START_PARENT, current_parent], check=False)
    if ancestry.returncode != 0:
        fail(f"M8 start parent {M8_START_PARENT} is not an ancestor of {current_parent}")

    horizon = specification["long_horizon"]
    require_equal("long dt", float(horizon["dt_s"]), 0.01)
    require_equal("long steps", int(horizon["steps"]), 12000)
    require_equal("long max_time", float(horizon["max_time_s"]), 120.0)
    require_equal("long horizon arithmetic",
                  float(horizon["dt_s"]) * int(horizon["steps"]),
                  float(horizon["max_time_s"]))
    require_equal("long hit radius", float(horizon["hit_radius_m"]), 1.0e-9)
    require_equal("terrain contract", horizon.get("terrain_enabled"), False)
    require_equal("LOS contract", horizon.get("los_occlusion_enabled"), False)
    require_equal("exit contract", horizon.get("expected_exit"), "timeout")
    require_equal("chunk contract", int(horizon["chunk_steps"]), 256)
    instances = specification["instances"]
    require_equal("instance counts", tuple(instances["counts"]), (64, 128))
    require_equal("maximum instance count", instances.get("maximum"), 128)
    require_equal("first instance ID", instances.get("id_first"), 0)
    require_equal("last instance ID", instances.get("id_last"), 127)
    require_equal("seed base", instances.get("seed_base"), 12345)
    require_equal("seed stride", instances.get("seed_stride"), 17)
    require_equal("CPU parallelism", instances.get("cpu_parallelism"), 6)
    datasets = specification["datasets"]
    uniform = datasets["uniform"]
    require_equal("uniform scenario path", uniform.get("shared_scenario"),
                  "generated/scenarios/uniform_long.json")
    require_equal("uniform fault profile", uniform.get("fault_profile"), "empty")
    require_equal("uniform physical scenario count",
                  uniform.get("physical_scenario_count"), 1)
    require_equal("uniform N=64 runtime", uniform.get("runtime_64"),
                  "generated/runtimes/uniform_n64.json")
    require_equal("uniform N=128 runtime", uniform.get("runtime_128"),
                  "generated/runtimes/uniform_n128.json")
    heterogeneous = datasets["heterogeneous"]
    require_equal("heterogeneous scenario pattern",
                  heterogeneous.get("scenario_pattern"),
                  "generated/scenarios/heterogeneous/scenario_${instance_id_04}.json")
    require_equal("heterogeneous runtime", heterogeneous.get("runtime_128"),
                  "generated/runtimes/heterogeneous_n128.json")
    require_equal("heterogeneous scenario count",
                  heterogeneous.get("physical_scenario_count"), 128)
    require_equal("profile cycle",
                  tuple(heterogeneous["profile_cycle"]),
                  PROFILE_CYCLE)
    require_equal("profile assignment", heterogeneous.get("profile_assignment"),
                  "profile_cycle[instance_id % 8]")
    require_equal("dropout probability",
                  float(heterogeneous["dropout_probability"]), 0.3)
    require_equal("fault profiles", tuple(specification["fault_profiles"]),
                  ("empty", *FAULT_PROFILES))
    require_equal("heterogeneous grid",
                  (heterogeneous["grid_rows"], heterogeneous["grid_columns"]),
                  (8, 16))
    require_equal("row coordinate formula",
                  heterogeneous["instance_coordinates"].get("row"),
                  "instance_id // 16")
    require_equal("column coordinate formula",
                  heterogeneous["instance_coordinates"].get("column"),
                  "instance_id % 16")
    formulas = {
        "missile_latitude_formula": "base + (row - 3.5) * 0.0001",
        "missile_longitude_formula": "base + (column - 7.5) * 0.0001",
        "missile_height_formula":
            "base + missile_height_m_cycle[instance_id % 5]",
        "missile_velocity_x_formula":
            "base + ((instance_id % 9) - 4) * 2.0",
        "missile_velocity_y_formula":
            "base + ((instance_id % 11) - 5) * 2.0",
        "missile_velocity_z_formula":
            "base + ((instance_id % 5) - 2) * 0.5",
        "target_latitude_formula":
            "base + ((column % 8) - 3.5) * 0.0001",
        "target_longitude_formula": "base + (row - 3.5) * 0.0002",
        "target_height_formula":
            "base + target_height_m_cycle[instance_id % 7]",
        "target_velocity_x_formula":
            "base + ((instance_id % 7) - 3) * 1.5",
        "target_velocity_y_formula":
            "base + ((instance_id % 9) - 4) * 1.5",
        "target_velocity_z_formula":
            "base + ((instance_id % 3) - 1) * 0.5",
    }
    perturbation = heterogeneous["perturbation"]
    for key, value in formulas.items():
        require_equal(f"perturbation {key}", perturbation.get(key), value)
    require_equal("missile height cycle",
                  perturbation.get("missile_height_m_cycle"),
                  [-20.0, -10.0, 0.0, 10.0, 20.0])
    require_equal("target height cycle",
                  perturbation.get("target_height_m_cycle"),
                  [-30.0, -20.0, -10.0, 0.0, 10.0, 20.0, 30.0])
    numeric_steps = {
        "missile_latitude_deg_per_row": 0.0001,
        "missile_longitude_deg_per_column": 0.0001,
        "missile_velocity_x_mps_cycle_step": 2.0,
        "missile_velocity_y_mps_cycle_step": 2.0,
        "missile_velocity_z_mps_cycle_step": 0.5,
        "target_latitude_deg_per_column_mod8": 0.0001,
        "target_longitude_deg_per_row": 0.0002,
        "target_velocity_x_mps_cycle_step": 1.5,
        "target_velocity_y_mps_cycle_step": 1.5,
        "target_velocity_z_mps_cycle_step": 0.5,
    }
    for key, value in numeric_steps.items():
        require_equal(f"perturbation {key}", float(perturbation[key]), value)

    acceptance = specification["acceptance"]
    require_equal("default rtol", float(acceptance["default_rtol"]), 1.0e-10)
    require_equal("default atol", float(acceptance["default_atol"]), 1.0e-12)
    require_equal("integer/event semantics",
                  acceptance.get("integer_and_event_semantics"), "exact")
    calibration = acceptance.get("field_calibrations")
    if not isinstance(calibration, list) or len(calibration) != 1:
        fail("M8 field calibration contract must contain exactly one entry")
    require_equal("quiet velocity-Y calibration profile",
                  calibration[0].get("profile"), "quiet")
    require_equal("quiet velocity-Y calibration field",
                  calibration[0].get("field"),
                  "SensorFrame.missile_vel_ecef_meas.y")
    require_equal("quiet velocity-Y calibration atol",
                  float(calibration[0]["atol"]), 1.0e-10)
    if not calibration[0].get("reason"):
        fail("M8 field calibration must record its M7 rationale")
    for key in ("require_deterministic_regeneration",
                "require_uniform_64_prefix_of_128", "require_unique_ids",
                "require_unique_seeds",
                "require_unique_heterogeneous_scenarios",
                "require_host_loader_probe"):
        require_equal(f"acceptance {key}", acceptance.get(key), True)


def build_models(specification: dict[str, Any], repo: Path) -> tuple[
        dict[str, dict[str, Any]], Counter[str]]:
    source = specification["source"]
    baseline_runtime = read_json(repo / source["baseline_runtime"])
    baseline_scenario = read_json(repo / source["baseline_scenario"])
    m4_cases = read_json(repo / source["validated_fault_matrix"])
    fault_cases = {item["name"]: item for item in m4_cases["cases"]}
    missing = set(FAULT_PROFILES) - set(fault_cases)
    if missing:
        fail(f"M4 validated fault profiles are missing: {sorted(missing)}")

    prefix = "gpu_workspace/configs/m8/generated"
    models: dict[str, dict[str, Any]] = {}
    models["faults/empty.json"] = {"schema_version": 1, "faults": []}
    for name in FAULT_PROFILES:
        faults = fault_cases[name].get("faults")
        if not isinstance(faults, list) or not faults:
            fail(f"M4 fault profile {name} is empty or invalid")
        models[f"faults/{name}.json"] = {
            "schema_version": 1,
            "faults": copy.deepcopy(faults),
        }

    uniform = make_long_scenario(baseline_scenario, specification)
    models["scenarios/uniform_long.json"] = uniform

    profile_counts: Counter[str] = Counter()
    heterogeneous_paths: list[str] = []
    heterogeneous_faults: list[str] = []
    dropout_probability = float(
        specification["datasets"]["heterogeneous"]["dropout_probability"])
    for instance_id in range(128):
        profile = PROFILE_CYCLE[instance_id % len(PROFILE_CYCLE)]
        profile_counts[profile] += 1
        scenario = make_long_scenario(baseline_scenario, specification)
        perturb_scenario(scenario, instance_id, specification)
        if profile == "quiet":
            zero_sensor_noise(scenario)
        elif profile == "dropout":
            set_dropout(scenario, dropout_probability)
        relative = f"scenarios/heterogeneous/scenario_{instance_id:04d}.json"
        models[relative] = scenario
        heterogeneous_paths.append(f"{prefix}/{relative}")
        fault_name = profile if profile in FAULT_PROFILES else "empty"
        heterogeneous_faults.append(f"{prefix}/faults/{fault_name}.json")

    uniform_scenario = f"{prefix}/scenarios/uniform_long.json"
    empty_faults = f"{prefix}/faults/empty.json"
    for name, count, port in (("uniform_n64", 64, 54000),
                              ("uniform_n128", 128, 55000)):
        models[f"runtimes/{name}.json"] = make_runtime(
            baseline_runtime, specification, f"m8_{name}", count, port,
            [uniform_scenario] * count, [empty_faults] * count)
    models["runtimes/heterogeneous_n128.json"] = make_runtime(
        baseline_runtime, specification, "m8_heterogeneous_n128", 128, 56000,
        heterogeneous_paths, heterogeneous_faults)
    return models, profile_counts


def model_fingerprint(models: dict[str, dict[str, Any]], prefix: str = "") -> dict[str, Any]:
    digest = hashlib.sha256()
    count = 0
    total = 0
    for relative in sorted(models):
        if prefix and not relative.startswith(prefix):
            continue
        blob = json_bytes(models[relative])
        count += 1
        total += len(blob)
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(len(blob)).encode("ascii"))
        digest.update(b"\0")
        digest.update(sha256_bytes(blob).encode("ascii"))
        digest.update(b"\n")
    return {"sha256": digest.hexdigest(), "file_count": count,
            "total_bytes": total}


def validate_models(models: dict[str, dict[str, Any]], profile_counts: Counter[str],
                    specification: dict[str, Any]) -> None:
    require_equal("generated file count", len(models), 138)
    require_equal("profile distribution", dict(profile_counts),
                  {name: 16 for name in PROFILE_CYCLE})
    uniform64 = models["runtimes/uniform_n64.json"]
    uniform128 = models["runtimes/uniform_n128.json"]
    heterogeneous = models["runtimes/heterogeneous_n128.json"]
    require_equal("uniform 64 prefix",
                  uniform64["instances"], uniform128["instances"][:64])
    for runtime, expected_count in ((uniform64, 64), (uniform128, 128),
                                    (heterogeneous, 128)):
        require_equal("runtime campaign count",
                      runtime["campaign"]["instance_count"], expected_count)
        require_equal("runtime instance count", len(runtime["instances"]),
                      expected_count)
        require_equal("runtime CPU parallelism",
                      runtime["campaign"]["max_parallel_instances"], 6)
        require_equal("runtime schedule", runtime["campaign"]["schedule"],
                      "PARALLEL")
        ids = [item["instance_id"] for item in runtime["instances"]]
        seeds = [item["random_seed"] for item in runtime["instances"]]
        require_equal("contiguous instance IDs", ids, list(range(expected_count)))
        require_equal("unique seeds", len(set(seeds)), expected_count)
        require_equal("seed sequence", seeds,
                      [12345 + 17 * index for index in range(expected_count)])
        require_equal("runtime network port pairing",
                      runtime["network"]["flight_control_base_port"],
                      runtime["network"]["environment_base_port"] + 1)
        if (runtime["network"]["flight_control_base_port"] +
                2 * (expected_count - 1) > 65535):
            fail("runtime network port range exceeds uint16")

    hashes = {
        sha256_bytes(json_bytes(models[
            f"scenarios/heterogeneous/scenario_{index:04d}.json"]))
        for index in range(128)
    }
    require_equal("unique heterogeneous scenario hashes", len(hashes), 128)
    uniform_sensors = models["scenarios/uniform_long.json"]["sensors"]
    quiet_reference = copy.deepcopy(models["scenarios/uniform_long.json"])
    zero_sensor_noise(quiet_reference)
    dropout_reference = copy.deepcopy(models["scenarios/uniform_long.json"])
    set_dropout(dropout_reference, float(
        specification["datasets"]["heterogeneous"]["dropout_probability"]))
    actual_profiles: Counter[str] = Counter()
    actual_faults: Counter[str] = Counter()
    for instance_id, instance in enumerate(heterogeneous["instances"]):
        profile = PROFILE_CYCLE[instance_id % len(PROFILE_CYCLE)]
        actual_profiles[profile] += 1
        scenario_relative = (
            f"scenarios/heterogeneous/scenario_{instance_id:04d}.json")
        require_equal(f"instance {instance_id} scenario reference",
                      instance["scenario"],
                      "gpu_workspace/configs/m8/generated/" + scenario_relative)
        require_equal(f"instance {instance_id} flight-control reference",
                      instance["flight_control"],
                      specification["source"]["baseline_flight_control"])
        fault_name = profile if profile in FAULT_PROFILES else "empty"
        require_equal(f"instance {instance_id} fault reference",
                      instance["faults"],
                      f"gpu_workspace/configs/m8/generated/faults/{fault_name}.json")
        actual_faults[fault_name] += 1
        sensors = models[scenario_relative]["sensors"]
        if profile == "quiet":
            require_equal(f"instance {instance_id} quiet sensor policy", sensors,
                          quiet_reference["sensors"])
        elif profile == "dropout":
            require_equal(f"instance {instance_id} dropout sensor policy", sensors,
                          dropout_reference["sensors"])
        else:
            require_equal(f"instance {instance_id} baseline sensor policy", sensors,
                          uniform_sensors)
    require_equal("runtime-derived profile distribution", dict(actual_profiles),
                  {name: 16 for name in PROFILE_CYCLE})
    require_equal("runtime-derived fault distribution", dict(actual_faults),
                  {"empty": 48, **{name: 16 for name in FAULT_PROFILES}})
    require_equal("planned/runtime profile distribution", actual_profiles,
                  profile_counts)
    horizon = specification["long_horizon"]
    for relative, scenario in models.items():
        if not relative.startswith("scenarios/"):
            continue
        require_equal(f"{relative} dt", scenario["simulation"]["dt"],
                      float(horizon["dt_s"]))
        require_equal(f"{relative} max_time",
                      scenario["simulation"]["max_time"],
                      float(horizon["max_time_s"]))
        require_equal(f"{relative} hit_radius",
                      scenario["simulation"]["hit_radius_m"],
                      float(horizon["hit_radius_m"]))
        require_equal(f"{relative} terrain",
                      scenario["map"]["enable_terrain"], False)
        require_equal(f"{relative} LOS",
                      scenario["map"]["enable_los_occlusion"], False)
        for key in ("tile_path", "tile_paths", "tile_index_path"):
            if key in scenario["map"]:
                fail(f"{relative} retains forbidden map asset key {key}")
        for vector in (scenario["missile"]["initial_lla_deg_m"],
                       scenario["missile"]["initial_velocity_ecef_mps"],
                       scenario["target"]["initial_lla_deg_m"],
                       scenario["target"]["initial_velocity_ecef_mps"]):
            if len(vector) != 3 or not all(math.isfinite(float(value)) for value in vector):
                fail(f"{relative} has invalid initial state")


def validate_budgets(specification: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
    layout = specification["layout_expectations"]
    trace_size = int(probe["device_trace_record_bytes"])
    state_size = int(probe["device_instance_state_bytes"])
    config_size = int(probe["device_instance_config_bytes"])
    require_equal("DeviceTraceRecord ABI", trace_size,
                  int(layout["device_trace_record_bytes"]))
    require_equal("DeviceInstanceState ABI", state_size,
                  int(layout["device_instance_state_bytes"]))
    require_equal("DeviceInstanceConfig ABI", config_size,
                  int(layout["device_instance_config_bytes"]))
    records = int(layout["full_trace_records_per_instance"])
    chunk_records = int(specification["long_horizon"]["chunk_steps"]) + 1
    budgets: dict[str, Any] = {
        "scope": layout["budget_scope"],
        "trace_record_bytes": trace_size,
        "instance_state_bytes": state_size,
        "instance_config_bytes": config_size,
        "full_trace_records_per_instance": records,
        "exact_trace_bytes_per_instance": trace_size * records,
        "counts": {},
    }
    require_equal("trace bytes per instance",
                  budgets["exact_trace_bytes_per_instance"],
                  int(layout["exact_trace_bytes_per_instance"]))
    for count in (64, 128):
        values = {
            "trace_bytes": trace_size * records * count,
            "state_bytes": state_size * count,
            "combined_bytes": (trace_size * records + state_size) * count,
            "chunk_256_single_trace_buffer_bytes": trace_size * chunk_records * count,
            "chunk_256_double_device_trace_bytes": 2 * trace_size * chunk_records * count,
            "device_state_plus_two_snapshots_bytes": 3 * state_size * count,
            "chunk_256_device_trace_and_state_bytes":
                2 * trace_size * chunk_records * count + 3 * state_size * count,
            "chunk_256_double_pinned_trace_and_state_bytes":
                2 * trace_size * chunk_records * count + 2 * state_size * count,
        }
        require_equal(f"N={count} resource budget", values,
                      layout["exact_budgets"][str(count)])
        budgets["counts"][str(count)] = values
    return budgets


def expected_files(models: dict[str, dict[str, Any]]) -> dict[str, bytes]:
    return {relative: json_bytes(value) for relative, value in models.items()}


def verify_tree(root: Path, expected: dict[str, bytes]) -> None:
    if not root.is_dir():
        fail(f"generated configuration tree is missing: {root}")
    actual = {
        path.relative_to(root).as_posix(): path
        for path in root.rglob("*") if path.is_file()
    }
    require_equal("generated file set", set(actual), set(expected))
    for relative, blob in expected.items():
        value = actual[relative].read_bytes()
        if value != blob:
            fail(f"generated file differs from deterministic model: {relative}")
        try:
            json.loads(value.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            fail(f"generated JSON is invalid: {relative}: {exc}")


def publish_tree(root: Path, expected: dict[str, bytes]) -> str:
    if root.exists():
        verify_tree(root, expected)
        return "reused_byte_identical_tree"
    temporary = root.with_name(root.name + ".tmp")
    if temporary.exists():
        fail(f"staging path already exists; inspect it before retrying: {temporary}")
    temporary.mkdir(parents=True)
    try:
        for relative, blob in expected.items():
            destination = temporary / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open("xb") as stream:
                stream.write(blob)
        verify_tree(temporary, expected)
        temporary.rename(root)
    except Exception:
        # Deliberately do not delete evidence after a failed publication.
        raise
    return "published_new_tree"


def run_probe(workspace: Path, generated: Path) -> dict[str, Any]:
    paths = [generated / "runtimes" / f"{name}.json" for name in RUNTIME_NAMES]
    command = ["bash", str(workspace / "tools/build_m8_config_probe.sh"),
               *(str(path) for path in paths)]
    print("m8_a1: build and run host loader/ABI probe", flush=True)
    result = subprocess.run(command, cwd=workspace.parent, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            check=False)
    if result.stdout:
        print(result.stdout.rstrip())
    if result.returncode != 0:
        fail("M8 config probe failed with status " + str(result.returncode) +
             "\nstdout:\n" + result.stdout + "\nstderr:\n" + result.stderr)
    lines = [line for line in result.stdout.splitlines() if line.strip()]
    if not lines:
        fail("M8 config probe produced no output")
    try:
        probe = json.loads(lines[-1])
    except json.JSONDecodeError as exc:
        fail(f"cannot parse M8 config probe output: {exc}")
    require_equal("probe status", probe.get("status"), "PASS")
    require_equal("probe uniform64 count", probe.get("uniform64_instances"), 64)
    require_equal("probe uniform128 count", probe.get("uniform128_instances"), 128)
    require_equal("probe heterogeneous count",
                  probe.get("heterogeneous128_instances"), 128)
    return probe


def dependency_fingerprint(repo: Path, roots: Sequence[Path]) -> str:
    """Fingerprint every project-owned source/header consumed by the probe."""
    files: set[Path] = set()
    for root in roots:
        if root.is_file():
            files.add(root.resolve())
        elif root.is_dir():
            files.update(path.resolve() for path in root.rglob("*")
                         if path.is_file() and not path.name.endswith(".tmp"))
        else:
            fail(f"probe dependency is missing: {root}")
    digest = hashlib.sha256()
    for path in sorted(files, key=lambda item: item.as_posix()):
        try:
            relative = path.relative_to(repo).as_posix()
        except ValueError:
            fail(f"probe dependency is outside the repository: {path}")
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(path.stat().st_size).encode("ascii"))
        digest.update(b"\0")
        digest.update(sha256_file(path).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def source_hashes(workspace: Path, specification: dict[str, Any]) -> dict[str, str]:
    repo = workspace.parent
    paths = {
        "generator": workspace / "tools/prepare_m8_cases.py",
        "matrix": workspace / "configs/m8/cases.json",
        "probe_source": workspace / "host_tests/m8_config_probe.cpp",
        "probe_build_script": workspace / "tools/build_m8_config_probe.sh",
        "m4_case_matrix": repo / specification["source"]["validated_fault_matrix"],
        "baseline_runtime": repo / specification["source"]["baseline_runtime"],
        "baseline_scenario": repo / specification["source"]["baseline_scenario"],
        "baseline_flight_control":
            repo / specification["source"]["baseline_flight_control"],
        "gpu_types": workspace / "gpu_campaign/include/gpu_types.cuh",
        "host_campaign_loader":
            workspace / "gpu_campaign/src/host_campaign_config.cpp",
        "m7_closure_report": repo / specification["source"]["m7_closure_report"],
    }
    result = {name: sha256_file(path) for name, path in paths.items()}
    result["probe_dependency_tree"] = dependency_fingerprint(repo, (
        repo / "external/Aircraft-Simu/common/src/config.c",
        workspace / "staging/common/vec3.c",
        workspace / "staging/common/matrix3.c",
        workspace / "staging/environment/earth_model.c",
        workspace / "staging/environment/geo_coordinate.c",
        workspace / "staging/environment/gravity_model.c",
        workspace / "staging/environment/atmosphere_model.c",
        workspace / "staging/environment/fault_injection.c",
        workspace / "staging/environment/fault_injection_loader.c",
        workspace / "gpu_campaign/src/host_scenario_mapper.cpp",
        workspace / "gpu_campaign/src/host_campaign_config.cpp",
        workspace / "staging/headers",
        workspace / "gpu_campaign/include",
    ))
    return result


def write_exclusive(path: Path, blob: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("xb") as stream:
            stream.write(blob)
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError:
        fail(f"refusing to replace existing acceptance evidence: {path}")


def make_receipt(workspace: Path, specification: dict[str, Any],
                 models: dict[str, dict[str, Any]], profile_counts: Counter[str],
                 probe: dict[str, Any], budgets: dict[str, Any],
                 publication: str) -> dict[str, Any]:
    repo = workspace.parent
    return {
        "schema_version": 1,
        "phase": "M8",
        "segment": "M8-A1",
        "status": "PASS",
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "complete_m8_gate": False,
        "next_segment": "M8-A2",
        "source": {
            "parent_commit": git(repo, ["rev-parse", "HEAD"],
                                 "resolve parent revision"),
            "m8_start_parent_commit": M8_START_PARENT,
            "aircraft_simu_commit": AIRCRAFT_COMMIT,
            "aircraft_simu_tracked_worktree_clean": True,
            "m7_closure_sha256": M7_REPORT_SHA256,
            "sha256": source_hashes(workspace, specification),
        },
        "inputs": {
            "publication": publication,
            "generated_root": "gpu_workspace/configs/m8/generated",
            "generated": model_fingerprint(models),
            "scenarios": model_fingerprint(models, "scenarios/"),
            "faults": model_fingerprint(models, "faults/"),
            "runtimes": model_fingerprint(models, "runtimes/"),
            "uniform": {
                "instance_counts": [64, 128],
                "physical_scenario_count": 1,
                "uniform64_is_exact_instance_prefix_of_uniform128": True,
            },
            "heterogeneous": {
                "instance_count": 128,
                "physical_scenario_count": 128,
                "unique_scenario_hash_count": 128,
                "profile_counts": dict(profile_counts),
            },
        },
        "host_loader_probe": probe,
        "resource_budget": budgets,
        "validation": {
            "deterministic_regeneration": "PASS",
            "exact_generated_file_set": "PASS",
            "all_generated_json_parseable": "PASS",
            "uniform_prefix": "PASS",
            "unique_instance_ids_and_seeds": "PASS",
            "heterogeneous_scenario_uniqueness": "PASS",
            "profile_distribution": "PASS",
            "host_loader_and_abi_probe": "PASS",
            "cpu_or_gpu_long_run_executed": False,
            "performance_claimed": False,
        },
    }


def validate_receipts(local: Path, tracked: Path, receipt: dict[str, Any],
                      workspace: Path, specification: dict[str, Any],
                      models: dict[str, dict[str, Any]],
                      profile_counts: Counter[str], probe: dict[str, Any],
                      budgets: dict[str, Any]) -> str:
    if not tracked.is_file():
        fail("tracked M8-A1 receipt is missing")
    tracked_blob = tracked.read_bytes()
    if local.is_file():
        require_equal("local/tracked receipt bytes", local.read_bytes(),
                      tracked_blob)
    else:
        print("m8_a1: build-local receipt is absent (allowed for a clean "
              "checkout); validating tracked evidence", flush=True)
    actual = read_json(tracked)
    timestamp_value = actual.get("created_at_utc")
    try:
        timestamp = datetime.fromisoformat(str(timestamp_value))
    except ValueError as exc:
        fail(f"receipt created_at_utc is invalid: {exc}")
    if timestamp.tzinfo is None:
        fail("receipt created_at_utc must be timezone-aware")
    publication = actual.get("inputs", {}).get("publication")
    if publication not in {"published_new_tree", "reused_byte_identical_tree"}:
        fail(f"receipt publication marker is invalid: {publication!r}")
    parent_commit = actual.get("source", {}).get("parent_commit")
    if not isinstance(parent_commit, str) or len(parent_commit) != 40:
        fail("receipt source.parent_commit is not a full commit ID")
    repo = workspace.parent
    git(repo, ["cat-file", "-e", parent_commit + "^{commit}"],
        "resolve receipt parent commit")
    current_parent = git(repo, ["rev-parse", "HEAD"],
                         "resolve current parent commit")
    ancestry = subprocess.run(
        ["git", "-C", str(repo), "merge-base", "--is-ancestor",
         parent_commit, current_parent], check=False)
    if ancestry.returncode != 0:
        fail(f"receipt parent {parent_commit} is not an ancestor of {current_parent}")

    expected = make_receipt(workspace, specification, models, profile_counts,
                            probe, budgets, publication)
    expected["created_at_utc"] = timestamp_value
    expected["source"]["parent_commit"] = parent_commit
    require_equal("complete receipt semantics", actual, expected)
    if receipt:
        require_equal("published receipt", actual, receipt)
    return sha256_bytes(tracked_blob)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path,
                        default=Path(__file__).resolve().parents[1])
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true",
                        help="publish generated configs and immutable A1 receipts")
    action.add_argument("--check", action="store_true",
                        help="verify the published configs and A1 receipts")
    parser.add_argument("--receipt", type=Path,
                        help="tracked receipt path (defaults to validation/m8_receipts/m8_a1_inputs.json)")
    args = parser.parse_args()

    workspace = args.workspace.resolve()
    repo = workspace.parent
    specification_path = workspace / "configs/m8/cases.json"
    specification = read_json(specification_path)
    validate_specification(specification, repo)
    models, profile_counts = build_models(specification, repo)
    models_again, counts_again = build_models(specification, repo)
    require_equal("deterministic in-memory regeneration",
                  expected_files(models_again), expected_files(models))
    require_equal("deterministic profile distribution", counts_again,
                  profile_counts)
    validate_models(models, profile_counts, specification)

    generated = workspace / "configs/m8/generated"
    expected = expected_files(models)
    tracked = (args.receipt.resolve() if args.receipt else
               workspace / "validation/m8_receipts/m8_a1_inputs.json")
    receipt_root = (workspace / "validation/m8_receipts").resolve()
    try:
        tracked.resolve().relative_to(receipt_root)
    except ValueError:
        fail(f"tracked receipt must be below {receipt_root}: {tracked}")
    local = workspace / "build/m8_a1_inputs/m8_a1_input_receipt.json"

    if args.write:
        if local.exists() or tracked.exists():
            fail("M8-A1 receipt already exists; use --check instead of replacing evidence")
        publication = publish_tree(generated, expected)
    else:
        verify_tree(generated, expected)
        publication = "verified_existing_tree"

    probe = run_probe(workspace, generated)
    budgets = validate_budgets(specification, probe)

    if args.write:
        receipt = make_receipt(workspace, specification, models, profile_counts,
                               probe, budgets, publication)
        blob = json_bytes(receipt)
        write_exclusive(tracked, blob)
        write_exclusive(local, blob)
        receipt_sha = validate_receipts(
            local, tracked, receipt, workspace, specification, models,
            profile_counts, probe, budgets)
        print("m8_a1: PASS published deterministic N=64/128 uniform and "
              "N=128 heterogeneous inputs")
    else:
        receipt_sha = validate_receipts(
            local, tracked, {}, workspace, specification, models,
            profile_counts, probe, budgets)
        print("m8_a1: PASS checked generated inputs, host loader/ABI, budgets, "
              "and tracked receipt (plus local copy when present)")
    print(f"m8_a1: receipt={tracked}")
    print(f"m8_a1: receipt_sha256={receipt_sha}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except M8Failure as exc:
        print(f"m8_a1: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
