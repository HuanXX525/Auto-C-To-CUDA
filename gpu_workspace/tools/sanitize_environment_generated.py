#!/usr/bin/env python3
"""Qualify and normalize audited helpers in the environment device slice."""

from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    source = root / "converted/environment/missile_plant_6dof.cu"
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

    geo = root / "converted/environment/geo_coordinate.cu"
    text = geo.read_text(encoding="utf-8")
    for signature in (
        "static SimStatus validate_lla",
        "static SimStatus position_to_local",
    ):
        text = text.replace(signature, "C2CUDA_HOST_DEVICE " + signature, 1)
    geo.write_text(text, encoding="utf-8")

    faults = root / "converted/environment/fault_injection.cu"
    text = faults.read_text(encoding="utf-8")
    for signature in (
        "static uint32_t target_valid_mask",
        "static uint32_t target_fault_mask",
        "static int target_actuator_index",
        "static int fault_is_sensor_frame_communication",
        "static SimStatus accumulate_fault",
        "static int fault_is_active",
    ):
        text = text.replace(signature, "C2CUDA_HOST_DEVICE " + signature, 1)
    fault_loop = (
        "for (index = 1; index <= (faults -> fault_count + (((long )1) - "
        "((long )(1 + ((size_t )0u))))) / 1; index += 1)"
    )
    actuator_loop = (
        "for (index = 1; index <= (((unsigned long )3u) + (((long )1) - "
        "((long )(1 + ((size_t )0u))))) / 1; index += 1)"
    )
    text = text.replace(
        fault_loop, "for (index = 0u; index < faults -> fault_count; ++index)", 1
    )
    text = text.replace(
        actuator_loop, "for (index = 0u; index < 3u; ++index)", 1
    )
    text = text.replace("1 * index + -1LL", "index")
    faults.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
