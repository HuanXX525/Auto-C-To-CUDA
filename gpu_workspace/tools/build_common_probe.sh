#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "${workspace_dir}/build"

for source in "${workspace_dir}"/converted/common/*.cu; do
    object="${workspace_dir}/build/$(basename "${source}" .cu).o"
    nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
        -I "${workspace_dir}/converted" "${source}" -o "${object}"
done

nvcc -std=c++14 -I "${workspace_dir}/staging/headers" \
    "${workspace_dir}/host_tests/common_compare.cpp" \
    "${workspace_dir}"/build/{vec3,matrix3,quaternion,random}.o \
    -o "${workspace_dir}/build/common_compare"

nvcc -std=c++14 -I "${workspace_dir}/staging/headers" \
    "${workspace_dir}/host_tests/ring_buffer_probe.cpp" \
    "${workspace_dir}/build/ring_buffer.o" \
    -o "${workspace_dir}/build/ring_buffer_probe"
"${workspace_dir}/build/ring_buffer_probe"
