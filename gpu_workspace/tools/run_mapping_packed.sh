#!/usr/bin/env bash
# Phase 4 thread-mapping experiment: packed lanes (one serial instance per
# lane) vs the serial baseline (one block per instance).
#
# Usage: run_mapping_packed.sh [steps] [instances_list]
#
# 1. Golden acceptance: for each N the packed kernel's instance-0 trace must
#    match the CPU golden baseline (the per-instance instruction stream is
#    unchanged, so any mismatch is a mapping bug).
# 2. Performance: serial vs packed kernel time and instance-steps/s.
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steps="${1:-64}"
instances_list="${2:-1 8 32 128}"
scenario="${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json"
aero_table="${workspace_dir}/build/campaign_trajectory_golden_table.aero"
golden="${workspace_dir}/golden/campaign_trajectory_golden.txt"
bin="${workspace_dir}/build/bin/gpu_campaign"

mkdir -p "${workspace_dir}/logs"

if [ ! -f "${aero_table}" ]; then
    echo "missing ${aero_table}; run tools/build_campaign_trajectory_golden.sh first" >&2
    exit 1
fi

for instances in ${instances_list}; do
    dump="${workspace_dir}/logs/packed_${instances}_steps_${steps}.txt"
    echo "== packed instances=${instances} steps=${steps} =="
    "${bin}" \
        --steps "${steps}" --instances "${instances}" \
        --repeats 1 --warmup 0 --mapping packed \
        --scenario "${scenario}" \
        --aero-file "${aero_table}" \
        --dump-trace "${dump}" \
        | tee "${workspace_dir}/logs/packed_${instances}_steps_${steps}.log"
    python3 "${workspace_dir}/tools/compare_golden_trace.py" \
        "${golden}" "${dump}"
done

echo
echo "== serial vs packed kernel time (64 steps, scenario dynamics) =="
for instances in ${instances_list}; do
    for mapping in serial packed; do
        "${bin}" \
            --steps "${steps}" --instances "${instances}" \
            --repeats 5 --warmup 1 --mapping "${mapping}" \
            --scenario "${scenario}" \
            --aero-file "${aero_table}" \
            | grep 'kernel_ms' \
            | sed "s/^/mapping=${mapping} instances=${instances} /"
    done
done

echo "mapping-packed: done"
