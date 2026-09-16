#!/usr/bin/env bash
# Phase 4 item 3: fault-injection verification.
#
# Rebuilds the CPU golden baselines (including the four-fault verification
# script: DROPOUT / RAMP_BIAS / HOLD_VALUE / BIAS on sensor targets), asserts
# the fault path actually fires and changes the FC behaviour, then verifies
# the GPU campaign against the fault golden (single instance + batch
# isolation).
#
# Needs a real GPU: run outside the sandbox.
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steps="${1:-64}"
instances_list="${2:-1 8 32 128}"
scenario="${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json"
aero_table="${workspace_dir}/build/campaign_trajectory_golden_table.aero"
faults_json="${workspace_dir}/build/faults_verification.json"
golden="${workspace_dir}/golden/campaign_trajectory_golden_fault.txt"
bin="${workspace_dir}/build/bin/gpu_campaign"
mkdir -p "${workspace_dir}/logs"

# 1. CPU golden baselines (also regenerates the aero table and the
#    verification fault script; asserts bitwise reproducibility).
"${workspace_dir}/tools/build_campaign_trajectory_golden.sh" \
    | tee "${workspace_dir}/logs/fault_golden.log"

# 2. The fault path must actually be exercised: non-zero active-fault steps.
fault_steps=$(sed -n 's/.*fault_active_steps=\([0-9]*\).*/\1/p' \
    "${workspace_dir}/logs/fault_golden.log" | tail -1)
if [ -z "${fault_steps}" ] || [ "${fault_steps}" -eq 0 ]; then
    echo "fault verification: FAIL - no step had an active fault" >&2
    exit 1
fi
echo "fault golden: fault_active_steps = ${fault_steps}"

# 3. Faults must change the behaviour versus the noise golden.
if cmp -s "${golden}" "${workspace_dir}/golden/campaign_trajectory_golden_noise.txt"; then
    echo "fault verification: FAIL - fault golden identical to noise golden" >&2
    exit 1
fi

# 4. Scope guard: actuator / communication faults must be rejected.
cat > "${workspace_dir}/build/faults_scope_bad.json" <<'JSONEOF'
{
  "schema_version": 1,
  "faults": [
    {
      "id": "actuator_scale",
      "enabled": true,
      "start_time_s": 0.1,
      "duration_s": 0.1,
      "target": "actuator.accel_x",
      "type": "SCALE",
      "value": 0.5
    }
  ]
}
JSONEOF
if "${bin}" --steps 8 --faults "${workspace_dir}/build/faults_scope_bad.json" \
    >/dev/null 2>&1; then
    echo "fault verification: FAIL - actuator fault was accepted" >&2
    exit 1
fi
echo "fault scope guard: actuator/communication faults rejected as expected"

# 5. GPU acceptance: single instance + batch isolation against the CPU
#    fault golden.
for instances in ${instances_list}; do
    dump="${workspace_dir}/logs/fault_${instances}_steps_${steps}.txt"
    echo "== instances=${instances} steps=${steps} (fault injection) =="
    "${bin}" \
        --steps "${steps}" --instances "${instances}" \
        --repeats 1 --warmup 0 \
        --scenario "${scenario}" \
        --aero-file "${aero_table}" \
        --faults "${faults_json}" \
        --dump-trace "${dump}" \
        | tee "${workspace_dir}/logs/fault_${instances}_steps_${steps}.log"
    python3 "${workspace_dir}/tools/compare_golden_trace.py" \
        "${golden}" "${dump}"
done

echo "fault-verification: PASS GPU matches CPU fault golden for N=${instances_list}"
