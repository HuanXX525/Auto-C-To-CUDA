#!/usr/bin/env python3
"""Apply audited device qualification to the M3 physical-model helpers."""

from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    actuator = root / "converted/environment/actuator_model.cu"
    text = actuator.read_text(encoding="utf-8")
    text = text.replace(
        "static double clamp_value",
        "C2CUDA_HOST_DEVICE static double clamp_value",
        1,
    )
    actuator.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
