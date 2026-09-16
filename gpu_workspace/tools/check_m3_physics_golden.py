#!/usr/bin/env python3
"""Check one focused M3 result directory against the recorded CPU golden."""

import argparse
import csv
import json
import math
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--golden", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--result", required=True)
    args = parser.parse_args()

    with Path(args.golden).open(encoding="utf-8") as stream:
        golden_document = json.load(stream)
    golden = golden_document["cases"][args.case]
    tolerance = golden_document["tolerance"]
    instance = Path(args.result) / "instance_0000"
    with (instance / "summary.json").open(encoding="utf-8") as stream:
        summary = json.load(stream)
    with (instance / "trajectory.csv").open(encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise SystemExit(f"m3_golden: FAIL {args.case}: empty trajectory")

    exact = ("simulation_steps", "exit_reason")
    for key in exact:
        if summary[key] != golden[key]:
            raise SystemExit(
                f"m3_golden: FAIL {args.case}: {key} "
                f"{summary[key]!r} != {golden[key]!r}"
            )
    for key in ("miss_distance", "time_of_closest_approach"):
        if not math.isclose(
            float(summary[key]), float(golden[key]),
            rel_tol=tolerance["relative"], abs_tol=tolerance["absolute"]
        ):
            raise SystemExit(
                f"m3_golden: FAIL {args.case}: {key} "
                f"{summary[key]!r} != {golden[key]!r}"
            )
    final = rows[-1]
    for key, expected in golden["final"].items():
        actual = float(final[key])
        if not math.isclose(
            actual, float(expected), rel_tol=tolerance["relative"],
            abs_tol=tolerance["absolute"]
        ):
            raise SystemExit(
                f"m3_golden: FAIL {args.case}: final.{key} "
                f"{actual!r} != {expected!r}"
            )
    print(f"m3_golden: PASS {args.case} {args.result}")


if __name__ == "__main__":
    main()
