#!/usr/bin/env python3
"""Check that instance zero is invariant across batch sizes."""

import re
import sys
from pathlib import Path


FIELDS = ("final_time", "final_x", "final_y", "exit_reason",
          "sensor_flags", "command_mode", "command_status", "command_ax")


def read_instance(path: Path, instance: int):
    text = path.read_text(encoding="utf-8")
    for line in text.splitlines():
        if "gpu_campaign:" not in line or "kernel_ms" in line:
            continue
        if "instance=" in line:
            match = re.search(r"instance=(\d+)", line)
            if match is None or int(match.group(1)) != instance:
                continue
        elif instance != 0:
            continue
        values = {}
        for field in FIELDS:
            match = re.search(rf"{field}=([^ ]+)", line)
            if match is None:
                raise RuntimeError(f"missing {field} in {path}")
            values[field] = match.group(1)
        return values
    raise RuntimeError(f"missing instance {instance} in {path}")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    steps = sys.argv[1] if len(sys.argv) > 1 else "8"
    base = read_instance(root / f"logs/multi_instances_1_steps_{steps}.log", 0)
    for instances in (2, 8, 32):
        current = read_instance(
            root / f"logs/multi_instances_{instances}_steps_{steps}.log", 0)
        if current != base:
            print(f"batch-isolation: FAIL instances={instances}")
            print(f"baseline={base}")
            print(f"current={current}")
            return 1
    print("batch-isolation: PASS instance=0 for N=1,2,8,32")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
