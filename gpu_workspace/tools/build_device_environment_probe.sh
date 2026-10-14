#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "${workspace_dir}/build"
nvcc -std=c++14 -I "${workspace_dir}/staging/headers" \
    -I "${workspace_dir}/gpu_campaign/include" \
    "${workspace_dir}/host_tests/device_environment_probe.cpp" \
    -o "${workspace_dir}/build/device_environment_probe"
"${workspace_dir}/build/device_environment_probe"
nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
    -I "${workspace_dir}/gpu_campaign/include" \
    "${workspace_dir}/host_tests/device_environment_device_link.cu" \
    -o "${workspace_dir}/build/device_environment_device_link.o"
nvcc -std=c++14 -dlink "${workspace_dir}/build/device_environment_device_link.o" \
    -o "${workspace_dir}/build/device_environment_device_link.dlink.o"

g++ -std=c++14 -x c++ -I "${workspace_dir}/staging/headers" -c \
    "${workspace_dir}/staging/common/vec3.c" -o "${workspace_dir}/build/force_vec3_host.o"
g++ -std=c++14 -x c++ -I "${workspace_dir}/staging/headers" -c \
    "${workspace_dir}/staging/common/quaternion.c" -o "${workspace_dir}/build/force_quaternion_host.o"
g++ -std=c++14 -x c++ -I "${workspace_dir}/staging/headers" -c \
    "${workspace_dir}/staging/common/matrix3.c" -o "${workspace_dir}/build/force_matrix3_host.o"
g++ -std=c++14 -x c++ -I "${workspace_dir}/staging/headers" -c \
    "${workspace_dir}/staging/environment/aero_model.c" -o "${workspace_dir}/build/force_aero_model_host.o"
g++ -std=c++14 -x c++ -I "${workspace_dir}/staging/headers" -c \
    "${workspace_dir}/staging/environment/aero_surrogate.c" -o "${workspace_dir}/build/force_aero_surrogate_host.o"
g++ -std=c++14 -x c++ -I "${workspace_dir}/staging/headers" -c \
    "${workspace_dir}/staging/environment_hostonly/aero_database.c" \
    -o "${workspace_dir}/build/force_aero_database_host.o"
g++ -std=c++14 -x c++ -I "${workspace_dir}/staging/headers" -c \
    "${workspace_dir}/../external/Aircraft-Simu/common/src/crc32.c" \
    -o "${workspace_dir}/build/force_crc32_host.o"
g++ -std=c++14 -I "${workspace_dir}/staging/headers" \
    -I "${workspace_dir}/gpu_campaign/include" -c \
    "${workspace_dir}/host_tests/device_force_probe.cpp" \
    -o "${workspace_dir}/build/device_force_probe.o"
g++ "${workspace_dir}/build/device_force_probe.o" \
    "${workspace_dir}/build/force_vec3_host.o" \
    "${workspace_dir}/build/force_quaternion_host.o" \
    "${workspace_dir}/build/force_matrix3_host.o" \
    "${workspace_dir}/build/force_aero_model_host.o" \
    "${workspace_dir}/build/force_aero_surrogate_host.o" \
    "${workspace_dir}/build/force_aero_database_host.o" \
    "${workspace_dir}/build/force_crc32_host.o" -lm \
    -o "${workspace_dir}/build/device_force_probe"
"${workspace_dir}/build/device_force_probe" \
    "${workspace_dir}/build/device_force_probe.aero"
nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
    -I "${workspace_dir}/gpu_campaign/include" \
    "${workspace_dir}/host_tests/device_force_device_link.cu" \
    -o "${workspace_dir}/build/device_force_device_link.o"
nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
    "${workspace_dir}/converted/common/vec3.cu" \
    -o "${workspace_dir}/build/device_force_vec3.o"
nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
    "${workspace_dir}/converted/common/quaternion.cu" \
    -o "${workspace_dir}/build/device_force_quaternion.o"
nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
    "${workspace_dir}/converted/common/matrix3.cu" \
    -o "${workspace_dir}/build/device_force_matrix3.o"
nvcc -std=c++14 -dlink "${workspace_dir}/build/device_force_device_link.o" \
    "${workspace_dir}/build/device_force_vec3.o" \
    "${workspace_dir}/build/device_force_quaternion.o" \
    "${workspace_dir}/build/device_force_matrix3.o" \
    -o "${workspace_dir}/build/device_force_device_link.dlink.o"
