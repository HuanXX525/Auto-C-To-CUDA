#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -ne 3 ]]; then
    echo "usage: $0 UNIFORM64 UNIFORM128 HETEROGENEOUS128" >&2
    exit 2
fi

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
headers_dir="${workspace_dir}/staging/headers"
build_dir="${workspace_dir}/build/m8_a1_inputs/probe"
mkdir -p "${build_dir}"

gcc -std=c11 -I "${headers_dir}" -c \
    "${workspace_dir}/../external/Aircraft-Simu/common/src/config.c" \
    -o "${build_dir}/config.o"

for source in common/vec3 common/matrix3 environment/earth_model \
    environment/geo_coordinate environment/gravity_model \
    environment/atmosphere_model; do
    object="$(basename "${source}")"
    g++ -std=c++14 -x c++ -I "${headers_dir}" -c \
        "${workspace_dir}/staging/${source}.c" \
        -o "${build_dir}/${object}.o"
done

g++ -std=c++14 -x c++ -I "${headers_dir}" -c \
    "${workspace_dir}/staging/environment/fault_injection.c" \
    -o "${build_dir}/fault_injection.o"
g++ -std=c++14 -x c++ -I "${headers_dir}" -c \
    "${workspace_dir}/staging/environment/fault_injection_loader.c" \
    -o "${build_dir}/fault_injection_loader.o"
g++ -std=c++14 -I "${headers_dir}" -I "${workspace_dir}/gpu_campaign/include" -c \
    "${workspace_dir}/gpu_campaign/src/host_scenario_mapper.cpp" \
    -o "${build_dir}/host_scenario_mapper.o"
g++ -std=c++14 -I "${headers_dir}" -I "${workspace_dir}/gpu_campaign/include" -c \
    "${workspace_dir}/gpu_campaign/src/host_campaign_config.cpp" \
    -o "${build_dir}/host_campaign_config.o"
g++ -std=c++14 -I "${headers_dir}" -I "${workspace_dir}/gpu_campaign/include" -c \
    "${workspace_dir}/host_tests/m8_config_probe.cpp" \
    -o "${build_dir}/m8_config_probe.o"

g++ \
    "${build_dir}/config.o" \
    "${build_dir}/vec3.o" \
    "${build_dir}/matrix3.o" \
    "${build_dir}/earth_model.o" \
    "${build_dir}/geo_coordinate.o" \
    "${build_dir}/gravity_model.o" \
    "${build_dir}/atmosphere_model.o" \
    "${build_dir}/fault_injection.o" \
    "${build_dir}/fault_injection_loader.o" \
    "${build_dir}/host_scenario_mapper.o" \
    "${build_dir}/host_campaign_config.o" \
    "${build_dir}/m8_config_probe.o" \
    -lm -o "${build_dir}/m8_config_probe"
"${build_dir}/m8_config_probe" "$1" "$2" "$3"
