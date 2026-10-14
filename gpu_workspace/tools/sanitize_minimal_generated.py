#!/usr/bin/env python3
"""Apply audited, minimal adaptations to generated vertical-slice code.

The converter currently lowers size_t loop variables in matrix3.c into
1-based expressions such as ``1 * row + -1LL``. Keeping the loop variables
signed avoids an unsigned conversion warning and makes the generated indexing
semantics explicit. This is an adaptation step, not a replacement for source
or algorithm review.
"""

from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    matrix = root / "converted/common/matrix3.cu"
    text = matrix.read_text(encoding="utf-8")
    for name in ("row", "column", "index"):
        text = text.replace(f"  size_t {name};", f"  int {name};")
    matrix.write_text(text, encoding="utf-8")

    ring = root / "converted/common/ring_buffer.cu"
    ring_text = ring.read_text(encoding="utf-8")
    for signature in ("static unsigned char *ring_buffer_storage",
                      "static const unsigned char *ring_buffer_storage_const"):
        ring_text = ring_text.replace(
            signature, "C2CUDA_HOST_DEVICE " + signature, 1)
    ring.write_text(ring_text, encoding="utf-8")


if __name__ == "__main__":
    main()
