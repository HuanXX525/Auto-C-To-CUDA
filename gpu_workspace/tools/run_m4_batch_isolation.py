#!/usr/bin/env python3
"""Verify that the M4 fault/LOS state remains isolated across GPU lanes."""

import argparse
import filecmp
import json
import shutil
import subprocess
from pathlib import Path


SIZES = (1, 8, 32, 128)
FILES = (
    "trajectory.csv",
    "trajectory_diagnostics.csv",
    "summary.json",
    "event_log.txt",
    "sensor_log.bin",
    "command_log.bin",
)


def normalized_packet_log(path):
    """Ignore only the protocol header's per-instance identity field."""
    data = bytearray(path.read_bytes())
    if len(data) % 64 != 0:
        raise SystemExit(f"m4_batch_isolation: invalid packet log: {path}")
    record_size = len(data) // 64
    if record_size < 16:
        raise SystemExit(f"m4_batch_isolation: short packet log: {path}")
    for offset in range(0, len(data), record_size):
        data[offset + 12:offset + 16] = b"\0\0\0\0"
    return bytes(data)


def make_runtime(source, destination, count):
    runtime = json.loads(source.read_text(encoding="utf-8"))
    runtime["campaign"]["instance_count"] = count
    runtime["campaign"]["max_parallel_instances"] = count
    template = runtime["instances"][0]
    runtime["instances"] = []
    for instance_id in range(count):
        instance = dict(template)
        instance["instance_id"] = instance_id
        runtime["instances"].append(instance)
    destination.write_text(json.dumps(runtime, indent=2) + "\n",
                           encoding="utf-8")


def compare_instance(reference, candidate, instance_id):
    for name in FILES:
        left = reference / name
        right = candidate / name
        if name.endswith("_log.bin"):
            same = normalized_packet_log(left) == normalized_packet_log(right)
        else:
            same = filecmp.cmp(left, right, shallow=False)
        if not same:
            raise SystemExit(
                f"m4_batch_isolation: FAIL N={instance_id}: {name} differs")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--source-runtime", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    workspace = Path(args.workspace).resolve()
    source = Path(args.source_runtime).resolve()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = workspace / "build/bin/gpu_campaign"
    reference = None
    for count in SIZES:
        runtime = output / f"runtime_n{count}.json"
        result = output / f"gpu_n{count}"
        make_runtime(source, runtime, count)
        if result.exists():
            shutil.rmtree(result)
        command = [
            str(binary), "--config", str(runtime), "--output-dir", str(result),
            "--trace-mode", "full", "--chunk-steps", "17",
            "--mapping", "packed", "--lanes-per-block", "4",
        ]
        subprocess.run(command, cwd=workspace, check=True)
        if reference is None:
            reference = result / "instance_0000"
        for instance_id in range(count):
            compare_instance(reference,
                             result / f"instance_{instance_id:04d}",
                             count)
        print(f"m4_batch_isolation: PASS instances={count}")


if __name__ == "__main__":
    main()
