#!/usr/bin/env python3
"""Qualify audited static helpers in the initial 6DOF device slice."""

from pathlib import Path


def main() -> None:
    source = (Path(__file__).resolve().parents[1] /
              "converted/environment/missile_plant_6dof.cu")
    text = source.read_text(encoding="utf-8")
    for signature in (
        "static Quat quat_add",
        "static Quat quat_scale",
        "static PlantContinuousState state_add_scaled",
        "static SimStatus evaluate_derivative",
        "static PlantDerivative rk4_weighted",
    ):
        text = text.replace(signature, "C2CUDA_HOST_DEVICE " + signature, 1)
    source.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
