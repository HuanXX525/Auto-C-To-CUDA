#!/usr/bin/env python3
"""Check LOS/fault packet semantics and event-log parity for one M4 case."""

import argparse
import struct
from pathlib import Path


SENSOR_PACKET_SIZE = 36 + 196
SENSOR_VALID_OFFSET = 36 + 188
SENSOR_FAULT_OFFSET = 36 + 192
LOS_OCCLUDED = 1


def packet_flags(path):
    data = path.read_bytes()
    if len(data) % SENSOR_PACKET_SIZE != 0:
        raise SystemExit(f"m4_packets: invalid sensor log size: {path}")
    values = []
    for offset in range(0, len(data), SENSOR_PACKET_SIZE):
        valid = struct.unpack_from("<I", data, offset + SENSOR_VALID_OFFSET)[0]
        fault = struct.unpack_from("<I", data, offset + SENSOR_FAULT_OFFSET)[0]
        values.append((valid, fault))
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", required=True)
    parser.add_argument("--cpu", required=True)
    parser.add_argument("--gpu", required=True)
    args = parser.parse_args()
    cpu = Path(args.cpu) / "instance_0000"
    gpu = Path(args.gpu) / "instance_0000"
    cpu_flags = packet_flags(cpu / "sensor_log.bin")
    gpu_flags = packet_flags(gpu / "sensor_log.bin")
    if len(cpu_flags) != len(gpu_flags):
        raise SystemExit(f"m4_packets: FAIL {args.case}: packet count differs")
    if cpu_flags != gpu_flags:
        raise SystemExit(f"m4_packets: FAIL {args.case}: CPU/GPU flags differ")
    if args.case in ("los_index_clear", "los_clear", "missing_flat"):
        if any(fault & LOS_OCCLUDED for _, fault in gpu_flags):
            raise SystemExit(f"m4_packets: FAIL {args.case}: unexpected LOS occlusion")
    if args.case == "los_index_occluded":
        if not any(fault & LOS_OCCLUDED for _, fault in gpu_flags):
            raise SystemExit(f"m4_packets: FAIL {args.case}: no LOS occlusion")
        if not any((valid & 1) == 0 for valid, _ in gpu_flags):
            raise SystemExit(f"m4_packets: FAIL {args.case}: seeker remained valid")
    cpu_event = (cpu / "event_log.txt").read_bytes()
    gpu_event = (gpu / "event_log.txt").read_bytes()
    if cpu_event != gpu_event:
        raise SystemExit(f"m4_packets: FAIL {args.case}: event logs differ")
    print(f"m4_packets: PASS {args.case} packets={len(gpu_flags)}")


if __name__ == "__main__":
    main()
