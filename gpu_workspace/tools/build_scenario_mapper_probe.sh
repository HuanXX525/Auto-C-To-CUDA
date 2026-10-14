#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
headers_dir="${workspace_dir}/staging/headers"
mkdir -p "${workspace_dir}/build"

gcc -std=c11 -I "${headers_dir}" -c \
    "${workspace_dir}/../external/Aircraft-Simu/common/src/config.c" \
    -o "${workspace_dir}/build/config_host.o"
g++ -std=c++14 -x c++ -I "${headers_dir}" -c \
    "${workspace_dir}/staging/common/vec3.c" \
    -o "${workspace_dir}/build/scenario_vec3_host.o"
g++ -std=c++14 -I "${headers_dir}" -I "${workspace_dir}/gpu_campaign/include" -c \
    "${workspace_dir}/gpu_campaign/src/host_scenario_mapper.cpp" \
    -o "${workspace_dir}/build/host_scenario_mapper.o"
g++ -std=c++14 -x c++ -I "${headers_dir}" -c \
    "${workspace_dir}/staging/environment/fault_injection_loader.c" \
    -o "${workspace_dir}/build/fault_injection_loader_host.o"
g++ -std=c++14 -x c++ -I "${headers_dir}" -c \
    "${workspace_dir}/staging/environment/fault_injection.c" \
    -o "${workspace_dir}/build/fault_injection_runtime_host.o"
g++ -std=c++14 -I "${headers_dir}" -I "${workspace_dir}/gpu_campaign/include" -c \
    "${workspace_dir}/host_tests/scenario_mapper_probe.cpp" \
    -o "${workspace_dir}/build/scenario_mapper_probe.o"
g++ "${workspace_dir}/build/scenario_mapper_probe.o" \
    "${workspace_dir}/build/host_scenario_mapper.o" \
    "${workspace_dir}/build/fault_injection_loader_host.o" \
    "${workspace_dir}/build/fault_injection_runtime_host.o" \
    "${workspace_dir}/build/scenario_vec3_host.o" \
    "${workspace_dir}/build/config_host.o" -lm \
    -o "${workspace_dir}/build/scenario_mapper_probe"
"${workspace_dir}/build/scenario_mapper_probe" \
    "${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json" \
    "${workspace_dir}/build/scenario_mapper_probe_scratch.json"
