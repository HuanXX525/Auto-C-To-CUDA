#!/usr/bin/env bash
# Phase 4 thread-mapping follow-up: block-size sweep and large-N comparison.
#
# The first packed experiment showed N=128 with 32 lanes/block creates only
# 4 blocks -> 4 SMs busy. This sweep tests:
#   1. lanes-per-block sweep at fixed N=128 (SM coverage vs warp sharing)
#   2. serial vs packed at large N (512/1024/2048) where serial hits the
#      per-SM block residency limit (~48 SM x 32 blocks) and packed should win
#
# Usage: run_mapping_sweep.sh [steps]
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steps="${1:-64}"
scenario="${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json"
aero_table="${workspace_dir}/build/campaign_trajectory_golden_table.aero"
bin="${workspace_dir}/build/bin/gpu_campaign"
out="${workspace_dir}/logs/mapping_sweep_steps_${steps}.txt"

mkdir -p "${workspace_dir}/logs"

if [ ! -f "${aero_table}" ]; then
    echo "missing ${aero_table}; run tools/build_campaign_trajectory_golden.sh first" >&2
    exit 1
fi

run() {
    local instances="$1" mapping="$2" lanes="$3"
    "${bin}" \
        --steps "${steps}" --instances "${instances}" \
        --repeats 5 --warmup 1 --mapping "${mapping}" \
        --threads-per-instance "${lanes}" \
        --scenario "${scenario}" \
        --aero-file "${aero_table}" \
        | grep 'kernel_ms' \
        | sed "s/^/N=${instances} mapping=${mapping} lanes=${lanes} /"
}

{
    echo "== lanes sweep (N=128) =="
    for lanes in 2 3 4 8 16 32; do
        run 128 packed "${lanes}"
    done
    echo
    echo "== lanes=4 at large N vs serial =="
    for instances in 512 1024 2048; do
        run "${instances}" serial 1
        run "${instances}" packed 4
    done
} | tee "${out}"

echo "mapping-sweep: results in ${out}"
