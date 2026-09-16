#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${workspace_dir}/.." && pwd)"
translator="${repo_dir}/build/bin/translate.out"

mkdir -p "${workspace_dir}/converted/environment" "${workspace_dir}/build"
"${translator}" --scan-dir "${workspace_dir}/staging/environment" \
    --output-dir "${workspace_dir}/converted/environment" \
    --include "${workspace_dir}/staging/headers" \
    --exclude generated \
    -rose:C99 >"${workspace_dir}/logs/missile_convert.log" 2>&1

python3 "${workspace_dir}/tools/sanitize_environment_generated.py"
python3 "${workspace_dir}/tools/sanitize_aero_surrogate_device.py"
python3 "${workspace_dir}/tools/sanitize_sensor_generated.py"
nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
    -I "${workspace_dir}/converted" \
    "${workspace_dir}/host_tests/plant_device_link_probe.cu" \
    -o "${workspace_dir}/build/plant_device_link_probe.o"
nvcc -std=c++14 -dlink "${workspace_dir}/build/plant_device_link_probe.o" \
    -o "${workspace_dir}/build/plant_device_link_probe.dlink.o"

nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
    -I "${workspace_dir}/converted" \
    "${workspace_dir}/converted/environment/aero_surrogate_device.cu" \
    -o "${workspace_dir}/build/aero_surrogate_device.o"

nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
    -I "${workspace_dir}/converted" \
    "${workspace_dir}/converted/environment/missile_plant_6dof.cu" \
    -o "${workspace_dir}/build/missile_plant_6dof.o"
nvcc -std=c++14 -I "${workspace_dir}/staging/headers" \
    "${workspace_dir}/host_tests/plant_compare.cpp" \
    "${workspace_dir}/build/missile_plant_6dof.o" \
    "${workspace_dir}"/build/{vec3,matrix3,quaternion,random}.o \
    -o "${workspace_dir}/build/plant_compare"
nvcc -std=c++14 -I "${workspace_dir}/staging/headers" \
    "${workspace_dir}/host_tests/aero_surrogate_compare.cpp" \
    "${workspace_dir}/build/aero_surrogate_device.o" \
    -o "${workspace_dir}/build/aero_surrogate_compare"
