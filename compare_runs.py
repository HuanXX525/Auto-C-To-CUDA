#!/usr/bin/env python3
"""
Compare two Aircraft-Simu campaign result directories using semantic fields.

Full traces compare trajectory/diagnostic CSVs and decoded sensor/command
packets.  If either directory is summary-only, trajectory and binary files are
omitted from the comparison while final summaries and event order remain
strictly checked.  GPU timing and path metadata are intentionally excluded.

Usage:
    python3 compare_runs.py <dir_a> <dir_b> [--rtol 1e-10] [--atol 1e-12] [--verbose]
"""

import json
import csv
import sys
import math
import argparse
import re
import struct
from pathlib import Path


INTEGER_TOKEN = re.compile(r"^[+-]?\d+$")

# These are the fields that describe the simulation result, rather than the
# machine or process that produced it.  Paths, UDP ports, software build
# labels and wall-clock measurements are intentionally excluded: they are
# expected to differ between the CPU and CUDA backends.  Keep this list
# explicit so a newly added metadata field cannot silently become a
# correctness criterion (or be silently ignored).
MANIFEST_FIELDS = (
    "schema_version", "instance_id", "protocol_version",
    "dt_s", "max_time_s", "random_seed", "initial_mass_kg",
    "initial_propellant_mass_kg", "gravity_enabled", "atmosphere_enabled",
    "aerodynamics_enabled", "aero_table_enabled", "aero_surrogate_enabled",
    "propulsion_enabled", "earth_rotation_enabled", "terrain_enabled",
    "terrain_los_occlusion_enabled", "terrain_missing_policy",
    "terrain_flat_fill_height_m", "terrain_cache_tile_count",
    "terrain_tile_path_count", "binary_logs", "event_log",
)

SUMMARY_FIELDS = (
    "hit_flag", "miss_distance", "time_of_closest_approach",
    "simulation_steps", "exit_reason", "fault_configured_count",
    "fault_start_count", "fault_end_count", "fault_active_step_count",
    "fault_sensor_affected_step_count", "fault_actuator_affected_step_count",
    "fault_max_concurrent_active", "terrain_cache_path_count",
    "terrain_cache_capacity", "terrain_cache_loaded_count",
    "terrain_cache_load_count", "terrain_cache_eviction_count",
    "diagnostic_sample_count", "max_quat_norm_error",
    "max_dcm_orthogonality_error", "min_mass_kg", "min_inertia_diag_kgm2",
    "aero_model_flags_or", "model_degradation_flags_or",
    "aero_extrapolated_sample_count",
)

CAMPAIGN_FIELDS = (
    "instance_count", "schedule", "completed_count", "failed_count",
    "summary_available_count", "hit_count", "min_miss_distance",
    "total_fault_start_count", "total_fault_end_count",
    "total_fault_sensor_affected_step_count",
    "total_fault_actuator_affected_step_count",
    "total_diagnostic_sample_count", "max_quat_norm_error",
    "max_dcm_orthogonality_error", "min_mass_kg", "min_inertia_diag_kgm2",
    "aero_model_flags_or", "model_degradation_flags_or",
    "total_aero_extrapolated_sample_count",
)

CAMPAIGN_INSTANCE_FIELDS = (
    "instance_id", "random_seed", "env_status", "fc_status",
    "summary_available", "hit_flag", "miss_distance", "exit_reason",
    "fault_start_count", "fault_end_count",
    "fault_sensor_affected_step_count", "fault_actuator_affected_step_count",
    "diagnostic_sample_count", "max_quat_norm_error",
    "max_dcm_orthogonality_error", "min_mass_kg", "min_inertia_diag_kgm2",
    "aero_model_flags_or", "model_degradation_flags_or",
    "aero_extrapolated_sample_count",
)

def csv_quantum(token):
    """Return the decimal serialization quantum for a floating CSV token."""
    text = token.strip().lower()
    mantissa, separator, exponent_text = text.partition("e")
    exponent = int(exponent_text) if separator else 0
    fractional_digits = len(mantissa.partition(".")[2])
    return 10.0 ** (exponent - fractional_digits)


def csv_float_close(text_a, text_b, value_a, value_b, rtol, atol):
    """Compare serialized floats without rejecting adjacent rounded bins.

    Aircraft-Simu deliberately emits fixed-width decimal CSV fields.  Two
    underlying values within the requested tolerance can therefore land in
    adjacent last-digit bins.  Half a serialization quantum from each file
    is included in the absolute tolerance; the requested numerical tolerance
    still governs values farther apart than the output precision can explain.
    """
    quantum_tolerance = 0.5 * (csv_quantum(text_a) + csv_quantum(text_b))
    rounding_slack = (
        4.0 * sys.float_info.epsilon * max(abs(value_a), abs(value_b), 1.0)
    )
    return math.isclose(
        value_a,
        value_b,
        rel_tol=rtol,
        abs_tol=max(atol, quantum_tolerance + rounding_slack),
    )


def load_json(path):
    try:
        with open(path, "r") as f:
            return json.load(f)
    except FileNotFoundError:
        return None
    except json.JSONDecodeError as e:
        print(f"JSON parse error in {path}: {e}")
        return None


def _compare_value(value_a, value_b, label, rtol, atol):
    """Compare one semantic JSON value and return a mismatch or ``None``."""
    # bool is a subclass of int in Python; test it first so true != 1.
    if isinstance(value_a, bool) or isinstance(value_b, bool):
        if type(value_a) is not type(value_b) or value_a != value_b:
            return f"  {label}: {value_a!r} != {value_b!r}"
        return None
    if isinstance(value_a, (int, float)) and isinstance(value_b, (int, float)):
        if isinstance(value_a, float) or isinstance(value_b, float):
            if not math.isclose(float(value_a), float(value_b),
                               rel_tol=rtol, abs_tol=atol):
                return (f"  {label}: {value_a!r} != {value_b!r} "
                        f"(diff={abs(float(value_a) - float(value_b)):.6e})")
        elif value_a != value_b:
            return f"  {label}: {value_a!r} != {value_b!r}"
        return None
    if type(value_a) is not type(value_b) or value_a != value_b:
        return f"  {label}: {value_a!r} != {value_b!r}"
    return None


def compare_json_fields(a, b, path_label, fields=None, rtol=None, atol=None):
    """Compare an explicit semantic field list from two JSON objects."""
    if fields is None:
        fields = tuple(sorted(set(a.keys()) | set(b.keys())))
    mismatches = []
    for key in fields:
        in_a = key in a
        in_b = key in b
        if not in_a or not in_b:
            mismatches.append(
                f"  {path_label}.{key}: "
                f"{'missing in A' if not in_a else 'missing in B'}")
            continue
        mismatch = _compare_value(a[key], b[key], f"{path_label}.{key}",
                                  rtol, atol)
        if mismatch is not None:
            mismatches.append(mismatch)
    return mismatches


def compare_json_files(path_a, path_b, label, fields=None, rtol=None, atol=None):
    """Compare selected semantic fields in two JSON files."""
    a = load_json(path_a)
    b = load_json(path_b)

    if a is None and b is None:
        return False, [f"  {label}: missing or invalid in both directories"]
    if a is None:
        return False, [f"  {label}: missing in dir A ({path_a})"]
    if b is None:
        return False, [f"  {label}: missing in dir B ({path_b})"]

    diffs = compare_json_fields(a, b, label, fields=fields,
                                rtol=rtol, atol=atol)
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

            if INTEGER_TOKEN.fullmatch(va.strip()) and INTEGER_TOKEN.fullmatch(vb.strip()):
                if int(va) != int(vb):
                    col_name = header[col_idx] if col_idx < len(header) else f"col_{col_idx}"
                    diffs.append(
                        f"  {label}: row {row_idx} col '{col_name}' "
                        f"({va} != {vb})"
                    )
                    diff_count += 1
                    if diff_count >= max_diffs_per_file:
                        break
                continue

            try:
                fa = float(va)
                fb = float(vb)
                is_float = True
            except ValueError:
                is_float = False

            if is_float:
                if not csv_float_close(va, vb, fa, fb, rtol, atol):
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
    """Compare the deterministic fields of an instance summary."""
    return compare_json_files(path_a, path_b, label, fields=SUMMARY_FIELDS,
                              rtol=rtol, atol=atol)


def compare_campaign(path_a, path_b, label, rtol, atol):
    """Compare campaign-level result fields while ignoring wall-clock data."""
    a = load_json(path_a)
    b = load_json(path_b)
    if a is None and b is None:
        return False, [f"  {label}: missing or invalid in both directories"]
    if a is None:
        return False, [f"  {label}: missing in dir A"]
    if b is None:
        return False, [f"  {label}: missing in dir B"]

    mismatches = compare_json_fields(a, b, label, fields=CAMPAIGN_FIELDS,
                                     rtol=rtol, atol=atol)
    instances_a = a.get("instances")
    instances_b = b.get("instances")
    if not isinstance(instances_a, list) or not isinstance(instances_b, list):
        mismatches.append(f"  {label}.instances: missing or not an array")
        return False, mismatches
    if len(instances_a) != len(instances_b):
        mismatches.append(
            f"  {label}.instances: length {len(instances_a)} != "
            f"{len(instances_b)}")
    for index, (entry_a, entry_b) in enumerate(zip(instances_a, instances_b)):
        if not isinstance(entry_a, dict) or not isinstance(entry_b, dict):
            mismatches.append(f"  {label}.instances[{index}]: not an object")
            continue
        mismatches.extend(compare_json_fields(
            entry_a, entry_b, f"{label}.instances[{index}]",
            fields=CAMPAIGN_INSTANCE_FIELDS, rtol=rtol, atol=atol))
    return len(mismatches) == 0, mismatches


def compare_bytes(path_a, path_b, label):
    """Compare a required byte log and return a standard result tuple."""
    if not path_a.exists() or not path_b.exists():
        missing = "missing in A" if not path_a.exists() else "missing in B"
        return False, [f"  {label}: {missing}"]
    if path_a.read_bytes() != path_b.read_bytes():
        return False, [f"  {label}: byte content differs"]
    return True, []


def compare_packet_file(path_a, path_b, label, packet_kind, rtol, atol):
    """Compare CPU/GPU protocol logs after decoding their stable wire format.

    The packet encoder includes a CRC over floating-point payload bytes.  CPU
    and GPU math can differ in the last bits while still satisfying the
    simulation tolerance, so comparing raw CRC/bytes would reject valid
    results.  Header enums, sequence numbers, flags and packet ordering remain
    exact; floating-point fields use the requested tolerance.
    """
    if not path_a.exists() or not path_b.exists():
        missing = "missing in A" if not path_a.exists() else "missing in B"
        return False, [f"  {label}: {missing}"]
    data_a = path_a.read_bytes()
    data_b = path_b.read_bytes()
    header_fmt = "<IHHHHII d II"
    header_size = struct.calcsize(header_fmt)
    if packet_kind == "sensor":
        payload_fmt = "<I" + ("d" * 23) + "II"
        packet_size = header_size + struct.calcsize(payload_fmt)
        integer_payload_indexes = {0, 24, 25}
    else:
        payload_fmt = "<I" + ("d" * 18) + "II"
        packet_size = header_size + struct.calcsize(payload_fmt)
        integer_payload_indexes = {0, 19, 20}
    diffs = []
    if len(data_a) % packet_size != 0 or len(data_b) % packet_size != 0:
        return False, [f"  {label}: invalid packet size"]
    count_a = len(data_a) // packet_size
    count_b = len(data_b) // packet_size
    if count_a != count_b:
        return False, [f"  {label}: packet count {count_a} != {count_b}"]
    for index in range(count_a):
        offset = index * packet_size
        header_a = struct.unpack_from(header_fmt, data_a, offset)
        header_b = struct.unpack_from(header_fmt, data_b, offset)
        for field_index in (0, 1, 2, 3, 4, 5, 6, 8):
            if header_a[field_index] != header_b[field_index]:
                diffs.append(
                    f"  {label}[{index}].header[{field_index}]: "
                    f"{header_a[field_index]!r} != {header_b[field_index]!r}")
        if not math.isclose(header_a[7], header_b[7], rel_tol=rtol,
                            abs_tol=atol):
            diffs.append(f"  {label}[{index}].header.sim_time differs")
        payload_a = struct.unpack_from(payload_fmt, data_a, offset + header_size)
        payload_b = struct.unpack_from(payload_fmt, data_b, offset + header_size)
        for field_index, (value_a, value_b) in enumerate(zip(payload_a, payload_b)):
            if field_index in integer_payload_indexes:
                if value_a != value_b:
                    diffs.append(
                        f"  {label}[{index}].payload[{field_index}]: "
                        f"{value_a!r} != {value_b!r}")
            elif not math.isclose(value_a, value_b, rel_tol=rtol,
                                  abs_tol=atol):
                diffs.append(
                    f"  {label}[{index}].payload[{field_index}] differs: "
                    f"{value_a!r} != {value_b!r}")
            if len(diffs) >= 10:
                diffs.append(f"  {label}: ... (showing first 10 differences)")
                return False, diffs
    return len(diffs) == 0, diffs


def trace_mode(directory):
    """Return ``full`` or ``summary`` from GPU metadata/file presence."""
    metadata = load_json(Path(directory) / "gpu_execution.json")
    if isinstance(metadata, dict) and metadata.get("trace_mode") == "summary":
        return "summary"
    for instance in Path(directory).glob("instance_[0-9][0-9][0-9][0-9]"):
        if not (instance / "trajectory.csv").exists():
            return "summary"
    return "full"


def compare_instance(dir_a, dir_b, inst_id, rtol, atol, full):
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
        fields=MANIFEST_FIELDS, rtol=rtol, atol=atol,
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

    if full:
        for filename in ("trajectory.csv", "trajectory_diagnostics.csv"):
            csv_ok, csv_diffs = compare_csv_files(
                path_a / filename, path_b / filename,
                f"{inst_dir}/{filename}", rtol, atol)
            if not csv_ok:
                all_ok = False
                all_diffs.extend(csv_diffs)

        # Sensor and command logs use the original CPU packet wire format.
        # Decode them so last-bit CPU/GPU floating-point differences do not
        # turn a valid protocol result into a false mismatch.
        for filename, packet_kind in (("sensor_log.bin", "sensor"),
                                      ("command_log.bin", "command")):
            if not (path_a / filename).exists() and not (path_b / filename).exists():
                continue
            binary_ok, binary_diffs = compare_packet_file(
                path_a / filename, path_b / filename,
                f"{inst_dir}/{filename}", packet_kind, rtol, atol)
            if not binary_ok:
                all_ok = False
                all_diffs.extend(binary_diffs)

    if (path_a / "event_log.txt").exists() or (path_b / "event_log.txt").exists():
        event_ok, event_diffs = compare_bytes(
            path_a / "event_log.txt", path_b / "event_log.txt",
            f"{inst_dir}/event_log.txt")
        if not event_ok:
            all_ok = False
            all_diffs.extend(event_diffs)

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

    # A summary run intentionally omits trajectory/diagnostic/binary files.
    # If either side is summary-only, compare the shared semantic payload
    # (manifest, summary and event order) and do not mistake omitted files
    # for a numerical mismatch.
    full_payload = trace_mode(dir_a) == "full" and trace_mode(dir_b) == "full"

    if campaign_a and campaign_b:
        count_a = campaign_a.get("instance_count", 0)
        count_b = campaign_b.get("instance_count", 0)
        print(f"campaign_summary.json: instance_count A={count_a}, B={count_b}")
    else:
        print("WARNING: campaign_summary.json missing, scanning instance directories...")
    instance_names = sorted({
        path.name
        for directory in (dir_a, dir_b)
        for path in directory.glob("instance_[0-9][0-9][0-9][0-9]")
        if path.is_dir()
    })
    print(f"Comparing {len(instance_names)} instances...")
    print()

    matched = 0
    mismatched = 0
    missing = 0

    for inst_dir in instance_names:
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

        try:
            inst_id = int(inst_dir.split("_", 1)[1])
        except (IndexError, ValueError):
            mismatched += 1
            print(f"[MISMATCH] {inst_dir}: invalid instance directory name")
            continue
        ok, diffs = compare_instance(
            dir_a, dir_b, inst_id, args.rtol, args.atol, full_payload)

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

    campaign_ok, campaign_diffs = compare_campaign(
        dir_a / "campaign_summary.json", dir_b / "campaign_summary.json",
        "campaign_summary.json", args.rtol, args.atol)
    if not campaign_ok:
        print("[MISMATCH] campaign_summary.json")
        if args.verbose:
            for diff in campaign_diffs:
                print(diff)
        mismatched += 1

    if mismatched > 0:
        sys.exit(1)
    elif missing > 0:
        sys.exit(1)
    else:
        sys.exit(0)


if __name__ == "__main__":
    main()
