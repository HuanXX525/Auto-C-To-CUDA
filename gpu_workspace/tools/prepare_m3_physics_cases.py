#!/usr/bin/env python3
"""Materialize focused M3 CPU/GPU cases from the immutable CPU baseline."""

import argparse
import json
from pathlib import Path


def set_nested(document, dotted_key, value):
    target = document
    parts = dotted_key.split(".")
    for part in parts[:-1]:
        target = target[part]
    target[parts[-1]] = value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    workspace = repo / "gpu_workspace"
    baseline = repo / "external/Aircraft-Simu/configs/baseline"
    with (baseline / "scenario.json").open(encoding="utf-8") as stream:
        base_scenario = json.load(stream)
    with (workspace / "configs/m3/cases.json").open(encoding="utf-8") as stream:
        specification = json.load(stream)

    for case in specification["cases"]:
        scenario = json.loads(json.dumps(base_scenario))
        for key, value in case["overrides"].items():
            set_nested(scenario, key, value)
        case_name = case["name"]
        scenario_path = output / f"scenario_{case_name}.json"
        runtime_path = output / f"runtime_{case_name}.json"
        cpu_output = output / f"cpu_{case_name}"
        flight_control = (
            baseline / "flight_control.json"
            if case["flight_control"] == "baseline"
            else workspace / "configs/m3/flight_control_zero.json"
        )
        with scenario_path.open("w", encoding="utf-8") as stream:
            json.dump(scenario, stream, indent=2)
            stream.write("\n")
        runtime = {
            "schema_version": 1,
            "campaign": {
                "campaign_id": f"m3_{case_name}",
                "instance_count": 1,
                "schedule": "PARALLEL",
                "max_parallel_instances": 1,
                "base_random_seed": 12345,
                "failure_strategy": "CONTINUE_ON_FAILURE",
            },
            "network": {
                "protocol_version_major": 1,
                "protocol_version_minor": 0,
                "environment_base_port": 55000,
                "flight_control_base_port": 55001,
                "host": "127.0.0.1",
            },
            "logging": {
                "output_dir": str(cpu_output),
                "instance_dir_template": "instance_${instance_id}",
                "binary_logs": True,
                "event_log": True,
                "flush_every_steps": 100,
            },
            "tools": {
                "environment_program": str(
                    workspace / "build/cpu_baseline/environment_sim"
                ),
                "flight_control_program": str(
                    workspace / "build/cpu_baseline/flight_control_sim"
                ),
            },
            "instances": [
                {
                    "enabled": True,
                    "instance_id": 0,
                    "scenario": str(scenario_path),
                    "flight_control": str(flight_control),
                    "faults": str(workspace / "configs/m3/faults_none.json"),
                    "random_seed": 12345,
                }
            ],
        }
        with runtime_path.open("w", encoding="utf-8") as stream:
            json.dump(runtime, stream, indent=2)
            stream.write("\n")
        print(f"m3_case_prepare: {case_name} {runtime_path}")


if __name__ == "__main__":
    main()
