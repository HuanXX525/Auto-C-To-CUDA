#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${workspace_dir}/.." && pwd)"
source_dir="${repo_dir}/external/Aircraft-Simu"
build_dir="${workspace_dir}/build/cpu_baseline"
mkdir -p "${build_dir}"

common_sources=("${source_dir}"/common/src/*.c)
environment_sources=("${source_dir}"/environment_sim/src/*.c)
flight_control_sources=("${source_dir}"/flight_control_sim/src/*.c)
common_includes=(-I "${source_dir}/common/include")

gcc -std=c11 -O2 "${common_includes[@]}" \
    -I "${source_dir}/environment_sim/include" \
    "${common_sources[@]}" "${environment_sources[@]}" -lm \
    -o "${build_dir}/environment_sim"

gcc -std=c11 -O2 "${common_includes[@]}" \
    -I "${source_dir}/flight_control_sim/include" \
    "${common_sources[@]}" "${flight_control_sources[@]}" -lm \
    -o "${build_dir}/flight_control_sim"

gcc -std=c11 -O2 "${common_includes[@]}" \
    "${common_sources[@]}" \
    "${source_dir}/tools/instance_manager/src/main.c" -lm \
    -o "${build_dir}/instance_manager"

echo "cpu_baseline_build: PASS ${build_dir}"
