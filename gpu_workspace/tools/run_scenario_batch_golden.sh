#!/usr/bin/env bash
# Batch-isolation verification with the full scenario (dynamics + sensor
# noise/delay/dropout): instance 0's trace must be identical (field by
# field) to the CPU noise golden baseline for every batch size N, proving
# the shared read-only aero table and per-instance sensor RNG streams
# introduce no cross-instance interference. Needs a real GPU: run outside
# the sandbox.
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steps="${1:-64}"
instances_list="${2:-1 2 8 32 64 128}"
scenario="${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json"
aero_table="${workspace_dir}/build/campaign_trajectory_golden_table.aero"
golden="${workspace_dir}/golden/campaign_trajectory_golden_noise.txt"
mkdir -p "${workspace_dir}/logs"

if [ ! -f "${aero_table}" ]; then
    "${workspace_dir}/tools/build_campaign_trajectory_golden.sh" \
        > /dev/null
fi

for instances in ${instances_list}; do
    dump="${workspace_dir}/logs/golden_batch_${instances}_steps_${steps}.txt"
    echo "== instances=${instances} steps=${steps} (scenario dynamics + sensors) =="
    "${workspace_dir}/build/bin/gpu_campaign" \
        --steps "${steps}" --instances "${instances}" \
        --repeats 1 --warmup 0 \
        --scenario "${scenario}" \
        --aero-file "${aero_table}" \
        --dump-trace "${dump}" \
        | tee "${workspace_dir}/logs/golden_batch_${instances}_steps_${steps}.log"
    python3 "${workspace_dir}/tools/compare_golden_trace.py" \
        "${golden}" "${dump}"
done

echo "scenario-batch-isolation: PASS instance 0 matches noise golden for N=${instances_list}"
