#!/usr/bin/env python3
"""
Compare two Aircraft-Simu campaign result directories.

Usage:
    python3 compare_runs.py <dir_a> <dir_b> [--rtol 1e-10] [--atol 1e-12] [--verbose]
"""

import json
import csv
import sys
import os
import math
import argparse
from pathlib import Path


def load_json(path):
    try:
        with open(path, "r") as f:
            return json.load(f)
    except FileNotFoundError:
        return None
    except json.JSONDecodeError as e:
        print(f"JSON parse error in {path}: {e}")
        return None


def compare_json_fields(a, b, path_label, fields=None, rtol=None, atol=None):
    """
    Compare specified fields in two dicts.
    If fields is None, compare all common keys.
    Returns list of mismatch strings.
    """
    mismatches = []
    keys = fields if fields is not None else set(a.keys()) | set(b.keys())

    for key in keys:
        if key == "instances":
            continue
        va = a.get(key)
        vb = b.get(key)

        if isinstance(va, float) and isinstance(vb, float):
            if rtol is not None and atol is not None:
                if not math.isclose(va, vb, rel_tol=rtol, abs_tol=atol):
                    mismatches.append(
                        f"  {path_label}.{key}: {va!r} != {vb!r} "
                        f"(diff={abs(va - vb):.6e})"
                    )
            else:
                if va != vb:
                    mismatches.append(
                        f"  {path_label}.{key}: {va!r} != {vb!r}"
                    )
        elif type(va) is not type(vb) or va != vb:
            mismatches.append(
                f"  {path_label}.{key}: {va!r} != {vb!r}"
            )

    return mismatches


def compare_json_files(path_a, path_b, label, rtol=None, atol=None):
    """Compare two JSON files. Returns (ok, diffs)."""
    a = load_json(path_a)
    b = load_json(path_b)

    if a is None and b is None:
        return True, []
    if a is None:
        return False, [f"  {label}: missing in dir A ({path_a})"]
    if b is None:
        return False, [f"  {label}: missing in dir B ({path_b})"]

    diffs = compare_json_fields(a, b, label, rtol=rtol, atol=atol)
    return (len(diffs) == 0), diffs


def compare_csv_files(path_a, path_b, label, rtol, atol):
    """
    Compare two CSV files row-by-row, column-by-column with numerical tolerance.
    Returns (ok, diffs).
    """
    try:
        with open(path_a, "r") as fa:
            reader_a = list(csv.reader(fa))
    except FileNotFoundError:
        return False, [f"  {label}: file missing in dir A ({path_a})"]

    try:
        with open(path_b, "r") as fb:
            reader_b = list(csv.reader(fb))
    except FileNotFoundError:
        return False, [f"  {label}: file missing in dir B ({path_b})"]

    diffs = []

    if len(reader_a) != len(reader_b):
        diffs.append(
            f"  {label}: row count differs: {len(reader_a)} vs {len(reader_b)}"
        )
        return False, diffs

    if not reader_a:
        return True, []

    header = reader_a[0]
    if reader_b[0] != header:
        diffs.append(
            f"  {label}: header differs\n"
            f"    A: {','.join(header)}\n"
            f"    B: {','.join(reader_b[0])}"
        )
        return False, diffs

    max_diffs_per_file = 10
    diff_count = 0

    for row_idx in range(1, len(reader_a)):
        row_a = reader_a[row_idx]
        row_b = reader_b[row_idx]

        if len(row_a) != len(row_b):
            diffs.append(
                f"  {label}: row {row_idx} column count differs: "
                f"{len(row_a)} vs {len(row_b)}"
            )
            diff_count += 1
            if diff_count >= max_diffs_per_file:
                break
            continue

        for col_idx in range(len(row_a)):
            va = row_a[col_idx]
            vb = row_b[col_idx]

            try:
                fa = float(va)
                fb = float(vb)
                is_float = True
            except ValueError:
                is_float = False

            if is_float:
                if not math.isclose(fa, fb, rel_tol=rtol, abs_tol=atol):
                    col_name = header[col_idx] if col_idx < len(header) else f"col_{col_idx}"
                    diffs.append(
                        f"  {label}: row {row_idx} col '{col_name}' "
                        f"({fa!r} != {fb!r}, diff={abs(fa - fb):.6e})"
                    )
                    diff_count += 1
                    if diff_count >= max_diffs_per_file:
                        break
            else:
                if va != vb:
                    col_name = header[col_idx] if col_idx < len(header) else f"col_{col_idx}"
                    diffs.append(
                        f"  {label}: row {row_idx} col '{col_name}' "
                        f"('{va}' != '{vb}')"
                    )
                    diff_count += 1
                    if diff_count >= max_diffs_per_file:
                        break

        if diff_count >= max_diffs_per_file:
            diffs.append(f"  {label}: ... ({diff_count} differences, showing first {max_diffs_per_file})")
            break

    return (len(diffs) == 0), diffs


def compare_summary(path_a, path_b, label, rtol, atol):
    """Compare summary.json with numerical tolerance on float fields."""
    a = load_json(path_a)
    b = load_json(path_b)

    if a is None and b is None:
        return True, []
    if a is None:
        return False, [f"  {label}: missing in dir A"]
    if b is None:
        return False, [f"  {label}: missing in dir B"]

    float_keys = {
        "miss_distance", "time_of_closest_approach",
        "max_quat_norm_error", "max_dcm_orthogonality_error",
        "min_mass_kg", "min_inertia_diag_kgm2",
    }

    mismatches = []
    for key in sorted(set(a.keys()) | set(b.keys())):
        va = a.get(key)
        vb = b.get(key)

        if isinstance(va, float) and isinstance(vb, float) and key in float_keys:
            if not math.isclose(va, vb, rel_tol=rtol, abs_tol=atol):
                mismatches.append(
                    f"  {label}.{key}: {va!r} != {vb!r} "
                    f"(diff={abs(va - vb):.6e})"
                )
        elif type(va) is not type(vb) or va != vb:
            mismatches.append(
                f"  {label}.{key}: {va!r} != {vb!r}"
            )

    return (len(mismatches) == 0), mismatches


def compare_instance(dir_a, dir_b, inst_id, rtol, atol, verbose):
    """
    Compare a single instance pair.
    Returns (ok, diffs_list).
    """
    inst_dir = f"instance_{inst_id:04d}"
    path_a = Path(dir_a) / inst_dir
    path_b = Path(dir_b) / inst_dir

    if not path_a.is_dir() and not path_b.is_dir():
        return True, []
    if not path_a.is_dir():
        return False, [f"  {inst_dir}: missing in dir A"]
    if not path_b.is_dir():
        return False, [f"  {inst_dir}: missing in dir B"]

    all_ok = True
    all_diffs = []

    manifest_ok, manifest_diffs = compare_json_files(
        path_a / "run_manifest.json", path_b / "run_manifest.json",
        f"{inst_dir}/run_manifest.json",
    )
    if not manifest_ok:
        all_ok = False
        all_diffs.extend(manifest_diffs)

    summary_ok, summary_diffs = compare_summary(
        path_a / "summary.json", path_b / "summary.json",
        f"{inst_dir}/summary.json",
        rtol, atol,
    )
    if not summary_ok:
        all_ok = False
        all_diffs.extend(summary_diffs)

    traj_ok, traj_diffs = compare_csv_files(
        path_a / "trajectory.csv", path_b / "trajectory.csv",
        f"{inst_dir}/trajectory.csv",
        rtol, atol,
    )
    if not traj_ok:
        all_ok = False
        all_diffs.extend(traj_diffs)

    diag_ok, diag_diffs = compare_csv_files(
        path_a / "trajectory_diagnostics.csv",
        path_b / "trajectory_diagnostics.csv",
        f"{inst_dir}/trajectory_diagnostics.csv",
        rtol, atol,
    )
    if not diag_ok:
        all_ok = False
        all_diffs.extend(diag_diffs)

    return all_ok, all_diffs


def main():
    parser = argparse.ArgumentParser(
        description="Compare two Aircraft-Simu campaign result directories."
    )
    parser.add_argument("dir_a", help="First campaign result directory")
    parser.add_argument("dir_b", help="Second campaign result directory")
    parser.add_argument(
        "--rtol", type=float, default=1e-10,
        help="Relative tolerance for float comparison (default: 1e-10)"
    )
    parser.add_argument(
        "--atol", type=float, default=1e-12,
        help="Absolute tolerance for float comparison (default: 1e-12)"
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="Print detailed diffs for all mismatched instances"
    )
    args = parser.parse_args()

    dir_a = Path(args.dir_a).resolve()
    dir_b = Path(args.dir_b).resolve()

    if not dir_a.is_dir():
        print(f"ERROR: {dir_a} is not a directory")
        sys.exit(2)
    if not dir_b.is_dir():
        print(f"ERROR: {dir_b} is not a directory")
        sys.exit(2)

    print(f"Comparing:")
    print(f"  A: {dir_a}")
    print(f"  B: {dir_b}")
    print(f"  rtol={args.rtol}, atol={args.atol}")
    print()

    campaign_a = load_json(dir_a / "campaign_summary.json")
    campaign_b = load_json(dir_b / "campaign_summary.json")

    if campaign_a and campaign_b:
        count_a = campaign_a.get("instance_count", 0)
        count_b = campaign_b.get("instance_count", 0)
        print(f"campaign_summary.json: instance_count A={count_a}, B={count_b}")
        if count_a != count_b and count_a == 0:
            print("WARNING: instance counts differ or are zero, auto-detecting...")
        instance_count = max(count_a, count_b)
    else:
        print("WARNING: campaign_summary.json missing, scanning instance directories...")
        instance_dirs = set()
        for d in dir_a.iterdir():
            if d.is_dir() and d.name.startswith("instance_"):
                try:
                    instance_dirs.add(int(d.name.split("_")[1]))
                except ValueError:
                    pass
        instance_count = max(instance_dirs) + 1 if instance_dirs else 64

    if instance_count == 0:
        instance_count = 64

    print(f"Comparing {instance_count} instances...")
    print()

    matched = 0
    mismatched = 0
    missing = 0

    for inst_id in range(instance_count):
        inst_dir = f"instance_{inst_id:04d}"
        path_a = dir_a / inst_dir
        path_b = dir_b / inst_dir

        exists_a = path_a.is_dir()
        exists_b = path_b.is_dir()

        if not exists_a and not exists_b:
            continue
        if not exists_a or not exists_b:
            missing += 1
            status = "MISSING"
            if not exists_a:
                print(f"[{status}] {inst_dir} ... (not found in A)")
            else:
                print(f"[{status}] {inst_dir} ... (not found in B)")
            continue

        ok, diffs = compare_instance(dir_a, dir_b, inst_id, args.rtol, args.atol, args.verbose)

        if ok:
            matched += 1
            print(f"[ MATCH ] {inst_dir}")
        else:
            mismatched += 1
            print(f"[MISMATCH] {inst_dir}")
            if args.verbose:
                for d in diffs:
                    print(d)

    print()
    print("=" * 50)
    print(f"Summary: {matched} matched, {mismatched} mismatched, {missing} missing")
    print("=" * 50)

    if mismatched > 0:
        sys.exit(1)
    elif missing > 0:
        sys.exit(1)
    else:
        sys.exit(0)


if __name__ == "__main__":
    main()
