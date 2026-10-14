#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
headers_dir="${workspace_dir}/staging/headers"
mkdir -p "${workspace_dir}/build"

gcc -std=c11 -I "${headers_dir}" -c \
    "${workspace_dir}/staging/environment_hostonly/aero_database.c" \
    -o "${workspace_dir}/build/aero_database_host.o"
gcc -std=c11 -I "${headers_dir}" -c \
    "${workspace_dir}/../external/Aircraft-Simu/common/src/crc32.c" \
    -o "${workspace_dir}/build/crc32_host.o"
gcc -std=c11 -I "${headers_dir}" -c \
    "${workspace_dir}/../external/Aircraft-Simu/environment_sim/src/map_tile.c" \
    -o "${workspace_dir}/build/map_tile_host.o"
g++ -std=c++14 -I "${headers_dir}" -I "${workspace_dir}/gpu_campaign/include" -c \
    "${workspace_dir}/gpu_campaign/src/host_environment_packer.cpp" \
    -o "${workspace_dir}/build/host_environment_packer.o"
g++ -std=c++14 -I "${headers_dir}" -I "${workspace_dir}/gpu_campaign/include" -c \
    "${workspace_dir}/host_tests/host_environment_packer_probe.cpp" \
    -o "${workspace_dir}/build/host_environment_packer_probe.o"
g++ "${workspace_dir}/build/host_environment_packer_probe.o" \
    "${workspace_dir}/build/host_environment_packer.o" \
    "${workspace_dir}/build/aero_database_host.o" \
    "${workspace_dir}/build/map_tile_host.o" \
    "${workspace_dir}/build/crc32_host.o" -lm \
    -o "${workspace_dir}/build/host_environment_packer_probe"
"${workspace_dir}/build/host_environment_packer_probe" \
    "${workspace_dir}/build/adapter_probe.aero" \
    "${workspace_dir}/build/adapter_probe.tile"
