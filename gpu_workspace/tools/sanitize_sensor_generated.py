#!/usr/bin/env python3
"""Mark the audited sensor helper call graph as device-callable."""

from pathlib import Path
from typing import Tuple


def qualify(path: Path, signatures: Tuple[str, ...]) -> None:
    text = path.read_text(encoding="utf-8")
    for signature in signatures:
        text = text.replace(signature, "C2CUDA_HOST_DEVICE " + signature, 1)
    path.write_text(text, encoding="utf-8")


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    qualify(root / "converted/environment/sensor_noise.cu", ("static double clamp_value",))
    qualify(root / "converted/environment/sensor_vector3.cu", (
        "static SimStatus delay_seconds_to_steps",
        "static Vec3 vector_from_components",
        "static SimStatus sample_truth",
    ))
    qualify(root / "converted/environment/sensor_seeker.cu", (
        "static SimStatus seeker_delay_steps",
        "static SimStatus apply_scalar_noise",
        "static SimStatus evaluate_truth",
        "static SimStatus sample_truth",
    ))


if __name__ == "__main__":
    main()
