#!/usr/bin/env python3
"""Mark the audited flight-control helper call graph as device-callable."""

from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    paths = {
        "converted/flight_control/autopilot.cu": (
            "static double clamp_scalar",
            "static Vec3 clamp_vec3_components",
            "static SimStatus build_velocity_axes",
            "static SimStatus autopilot_config_validate",
        ),
        "converted/flight_control/command_manager.cu": (
            "static Vec3 limit_norm",
            "static Vec3 limit_rate",
        ),
        "converted/flight_control/fc_state.cu": (
            "static FcMode advance_mode",
            "static SimStatus register_default_tasks",
            "static SimStatus register_configured_tasks",
        ),
        "converted/flight_control/safety_monitor.cu": (
            "static int sensor_frame_measurements_finite",
        ),
    }
    for relative, signatures in paths.items():
        path = root / relative
        text = path.read_text(encoding="utf-8")
        for signature in signatures:
            text = text.replace(signature, "C2CUDA_HOST_DEVICE " + signature, 1)
        if relative.endswith("command_manager.cu"):
            text = text.replace("1 * index + -1LL", "index")
        path.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
