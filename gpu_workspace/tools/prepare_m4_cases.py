#!/usr/bin/env python3
"""Materialize M4 LOS/fault cases from the immutable CPU baseline."""

import argparse
import json
import subprocess
from pathlib import Path


def set_nested(document, dotted_key, value):
    target = document
    parts = dotted_key.split(".")
    for part in parts[:-1]:
        target = target[part]
    target[parts[-1]] = value


def write_tile(generator, path, height, bounds=None):
    command = [str(generator), str(path), str(height)]
    if bounds is not None:
        command.extend(str(value) for value in bounds)
    subprocess.run(command, check=True)


def terrain_index_name(terrain_name):
    return "index_borderline" if terrain_name == "borderline" else terrain_name


def terrain_setup(output, generator):
    """Create bounded overlapping tiles; order is the CPU selection order."""
    bounds = (29.99, 30.01, 120.03, 120.05)
    write_tile(generator, output / "tile_first.tile", 0.0, bounds)
    write_tile(generator, output / "tile_high.tile", 2000.0, bounds)
    write_tile(generator, output / "tile_border.tile", 998.0, bounds)
    (output / "index_clear.txt").write_text(
        "# first covering tile wins\ntile_first.tile\ntile_high.tile\n",
        encoding="utf-8")
    (output / "index_occluded.txt").write_text(
        "tile_high.tile\ntile_first.tile\n", encoding="utf-8")
    (output / "index_borderline.txt").write_text(
        "tile_border.tile\n", encoding="utf-8")


def make_scenario(base, terrain_name):
    scenario = json.loads(json.dumps(base))
    scenario["simulation"]["max_time"] = 0.64
    scenario["simulation"]["hit_radius_m"] = 0.01
    mapping = scenario["map"]
    mapping["enable_terrain"] = terrain_name != "none"
    mapping["enable_los_occlusion"] = terrain_name != "none"
    mapping["missing_tile_policy"] = (
        "ERROR" if terrain_name == "missing_error" else "FLAT_FILL")
    mapping["terrain"]["flat_fill_height_m"] = 0.0
    mapping.pop("tile_path", None)
    mapping.pop("tile_paths", None)
    mapping.pop("tile_index_path", None)
    if terrain_name in ("index_clear", "index_occluded", "borderline"):
        mapping["tile_index_path"] = f"{terrain_index_name(terrain_name)}.txt"
    return scenario


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--generator", required=True)
    args = parser.parse_args()
    repo = Path(args.repo).resolve()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    workspace = repo / "gpu_workspace"
    baseline = repo / "external/Aircraft-Simu/configs/baseline"
    base_scenario = json.loads((baseline / "scenario.json").read_text(encoding="utf-8"))
    specification = json.loads(
        (workspace / "configs/m4/cases.json").read_text(encoding="utf-8"))
    terrain_setup(output, Path(args.generator).resolve())
    for index, case in enumerate(specification["cases"]):
        name = case["name"]
        scenario_path = output / f"scenario_{name}.json"
        faults_path = output / f"faults_{name}.json"
        runtime_path = output / f"runtime_{name}.json"
        cpu_output = output / f"cpu_{name}"
        scenario = make_scenario(base_scenario, case["terrain"])
        scenario_path.write_text(json.dumps(scenario, indent=2) + "\n",
                                 encoding="utf-8")
        if case["terrain"] in ("index_clear", "index_occluded", "borderline"):
            index_path = output / f"{terrain_index_name(case['terrain'])}.txt"
            # The CPU instance manager launches environment_sim from its
            # caller's working directory, whereas the GPU loader resolves
            # relative assets against the scenario file.  Use one normalized
            # absolute reference so both paths consume the same index.
            scenario["map"]["tile_index_path"] = str(index_path.resolve())
            scenario_path.write_text(json.dumps(scenario, indent=2) + "\n",
                                     encoding="utf-8")
        faults = {"schema_version": 1, "faults": case["faults"]}
        faults_path.write_text(json.dumps(faults, indent=2) + "\n",
                               encoding="utf-8")
        runtime = {
            "schema_version": 1,
            "campaign": {
                "campaign_id": f"m4_{name}", "instance_count": 1,
                "schedule": "PARALLEL", "max_parallel_instances": 1,
                "base_random_seed": 12345,
                "failure_strategy": "CONTINUE_ON_FAILURE"
            },
            "network": {
                "protocol_version_major": 1, "protocol_version_minor": 0,
                "environment_base_port": 57000 + index * 4,
                "flight_control_base_port": 57001 + index * 4,
                "host": "127.0.0.1"
            },
            "logging": {
                "output_dir": str(cpu_output),
                "instance_dir_template": "instance_${instance_id}",
                "binary_logs": True, "event_log": True,
                "flush_every_steps": 100
            },
            "tools": {
                "environment_program": str(workspace / "build/cpu_baseline/environment_sim"),
                "flight_control_program": str(workspace / "build/cpu_baseline/flight_control_sim")
            },
            "instances": [{
                "enabled": True, "instance_id": 0,
                "scenario": str(scenario_path),
                "flight_control": str(baseline / "flight_control.json"),
                "faults": str(faults_path), "random_seed": 12345
            }]
        }
        runtime_path.write_text(json.dumps(runtime, indent=2) + "\n",
                                encoding="utf-8")
        print(f"m4_case_prepare: {name} {runtime_path}")


if __name__ == "__main__":
    main()
