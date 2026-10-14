#!/usr/bin/env python3
"""Compare a gpu_campaign --dump-trace file against the CPU golden baseline.

Both files use the column layout:
    step time pos_ecef[3] vel_ecef[3] attitude_wxyz[4] force_b_n[3]
    moment_b_nm[3] accel_cmd_ecef[3] mach density aero_flags command_mode
    command_status

Doubles are compared with a relative+absolute tolerance (nvcc may contract
multiply-add, so bit-exactness is not required); integers must match exactly.

Usage: compare_golden_trace.py GOLDEN TRACE [--rtol 1e-10] [--atol 1e-12]
"""
import argparse
import sys

COLUMNS = [
    "step", "time",
    "pos_x", "pos_y", "pos_z",
    "vel_x", "vel_y", "vel_z",
    "qw", "qx", "qy", "qz",
    "fx", "fy", "fz",
    "mx", "my", "mz",
    "ax", "ay", "az",
    "mach", "density", "aero_flags", "command_mode", "command_status",
]
INT_COLUMNS = {"step", "aero_flags", "command_mode", "command_status"}


def load(path):
    rows = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split()
            if len(fields) != len(COLUMNS):
                raise ValueError(f"{path}: expected {len(COLUMNS)} columns, "
                                 f"got {len(fields)}: {line[:80]}")
            rows.append(fields)
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("golden")
    parser.add_argument("trace")
    parser.add_argument("--rtol", type=float, default=1.0e-10)
    parser.add_argument("--atol", type=float, default=1.0e-12)
    parser.add_argument(
        "--tol", type=float, default=None,
        help="legacy option: set both --rtol and --atol to the same value")
    parser.add_argument(
        "--allow-zero-tail", action="store_true",
        help="trace may have extra rows beyond the golden (surface-collision "
             "early termination) when every extra row is all-zero")
    args = parser.parse_args()
    if args.tol is not None:
        args.rtol = args.tol
        args.atol = args.tol
    if args.rtol < 0.0 or args.atol < 0.0:
        parser.error("tolerances must be non-negative")

    golden = load(args.golden)
    trace = load(args.trace)
    if len(golden) != len(trace):
        if (args.allow_zero_tail and len(trace) > len(golden) and
                all(all(field == "0" for field in row)
                    for row in trace[len(golden):])):
            print(f"note: {len(trace) - len(golden)} all-zero tail rows "
                  f"beyond the golden (early termination)")
        else:
            print(f"FAIL: row count golden={len(golden)} trace={len(trace)}")
            return 1

    worst = {}  # column -> (abs_diff, rel_diff, golden value, trace value, step)
    failures = 0
    for row_index, (golden_row, trace_row) in enumerate(zip(golden, trace)):
        for column, golden_field, trace_field in zip(COLUMNS, golden_row,
                                                     trace_row):
            if column in INT_COLUMNS:
                if golden_field != trace_field:
                    print(f"FAIL: row {row_index} column {column}: "
                          f"golden={golden_field} trace={trace_field}")
                    failures += 1
                continue
            golden_value = float(golden_field)
            trace_value = float(trace_field)
            abs_diff = abs(golden_value - trace_value)
            scale = max(abs(golden_value), abs(trace_value))
            rel_diff = abs_diff / scale if scale > 0.0 else abs_diff
            if abs_diff > args.atol + args.rtol * abs(golden_value):
                print(f"FAIL: row {row_index} column {column}: "
                      f"golden={golden_value!r} trace={trace_value!r} "
                      f"rel={rel_diff:.3e}")
                failures += 1
            if column not in worst or rel_diff > worst[column][1]:
                worst[column] = (abs_diff, rel_diff, golden_value,
                                 trace_value, row_index)

    print(f"rows={len(golden)} rtol={args.rtol:g} atol={args.atol:g} "
          f"max_rel_deviations (worst columns):")
    for column in COLUMNS:
        if column in worst:
            abs_diff, rel_diff, golden_value, trace_value, row_index = \
                worst[column]
            print(f"  {column:14s} rel={rel_diff:.3e} abs={abs_diff:.3e} "
                  f"at row {row_index}")
    if failures:
        print(f"FAIL: {failures} mismatches")
        return 1
    print("PASS: device trace matches the CPU golden baseline")
    return 0


if __name__ == "__main__":
    sys.exit(main())
