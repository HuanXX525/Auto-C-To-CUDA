#!/usr/bin/env python3
from pathlib import Path


def main() -> None:
    source = (Path(__file__).resolve().parents[1] /
              "converted/environment/aero_surrogate_device.cu")
    text = source.read_text(encoding="utf-8")
    text = text.replace("static int term_is_finite_device",
                        "C2CUDA_HOST_DEVICE static int term_is_finite_device", 1)
    text = text.replace("static double term_evaluate_device",
                        "C2CUDA_HOST_DEVICE static double term_evaluate_device", 1)
    source.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
