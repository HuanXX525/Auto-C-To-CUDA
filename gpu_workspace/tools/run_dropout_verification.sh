#!/usr/bin/env bash
# Phase 4 item 2 completion: real sensor dropout verification.
#
# Derives a dropout scenario from the baseline (dropout_probability=0.3 on
# all four sensors), builds the CPU dropout golden baseline, then verifies
# the GPU campaign against it (single instance + batch isolation). The
# golden header records the per-sensor dropped-sample counts, which must be
# non-zero for the run to count as exercising the dropout path.
#
# Needs a real GPU: run outside the sandbox.
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steps="${1:-64}"
instances_list="${2:-1 8 32 128}"
baseline="${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json"
aero_table="${workspace_dir}/build/campaign_trajectory_golden_table.aero"
dropout_scenario="${workspace_dir}/build/scenario_dropout_30pct.json"
golden="${workspace_dir}/golden/campaign_trajectory_golden_dropout.txt"
bin="${workspace_dir}/build/bin/gpu_campaign"
mkdir -p "${workspace_dir}/logs" "${workspace_dir}/build"

# 1. Derive the dropout scenario from baseline (only the four
#    dropout_probability fields change; determinism comes from the seeds).
python3 - "${baseline}" "${dropout_scenario}" <<'PYEOF'
import json
import sys

with open(sys.argv[1]) as handle:
    scenario = json.load(handle)
for name in ("imu", "accelerometer", "speedometer", "seeker"):
    scenario["sensors"][name]["dropout_probability"] = 0.3
with open(sys.argv[2], "w") as handle:
    json.dump(scenario, handle, indent=2)
    handle.write("\n")
PYEOF

# 2. CPU dropout golden baseline (also asserts bitwise reproducibility and
#    regenerates the shared aero table).
"${workspace_dir}/tools/build_campaign_trajectory_golden.sh" > /dev/null
"${workspace_dir}/build/campaign_trajectory_golden" \
    "${dropout_scenario}" \
    "${aero_table}" \
    "${golden}" sensors | tee "${workspace_dir}/logs/dropout_golden.log"

# 3. The dropout path must actually be exercised: non-zero drop counts.
total_drops=$(sed -n 's/.*dropout_events=imu:\([0-9]*\),accel:\([0-9]*\),speed:\([0-9]*\),seeker:\([0-9]*\).*/\1+\2+\3+\4/p' \
    "${workspace_dir}/logs/dropout_golden.log" | bc)
if [ -z "${total_drops}" ] || [ "${total_drops}" -eq 0 ]; then
    echo "dropout verification: FAIL - no sensor dropped a sample" >&2
    exit 1
fi
echo "dropout golden: total dropped samples = ${total_drops}"

# 4. Dropout must change the behaviour: the dropout golden differs from the
#    noise golden of the unmodified baseline.
if cmp -s "${golden}" "${workspace_dir}/golden/campaign_trajectory_golden_noise.txt"; then
    echo "dropout verification: FAIL - dropout golden identical to noise golden" >&2
    exit 1
fi

# 5. GPU acceptance: single instance + batch isolation against the CPU
#    dropout golden.
for instances in ${instances_list}; do
    dump="${workspace_dir}/logs/dropout_${instances}_steps_${steps}.txt"
    echo "== instances=${instances} steps=${steps} (dropout 30%) =="
    "${bin}" \
        --steps "${steps}" --instances "${instances}" \
        --repeats 1 --warmup 0 \
        --scenario "${dropout_scenario}" \
        --aero-file "${aero_table}" \
        --dump-trace "${dump}" \
        | tee "${workspace_dir}/logs/dropout_${instances}_steps_${steps}.log"
    python3 "${workspace_dir}/tools/compare_golden_trace.py" \
        "${golden}" "${dump}"
done

echo "dropout-verification: PASS GPU matches CPU dropout golden for N=${instances_list}"
