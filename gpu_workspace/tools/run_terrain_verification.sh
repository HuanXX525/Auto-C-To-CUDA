#!/usr/bin/env bash
# Phase 4 item 4: terrain-dynamics verification.
#
# Rebuilds the CPU golden baselines (including the two terrain tiles: flat
# 990 m fly-over and flat 999.5 m mid-run surface collision), then verifies
# the GPU campaign against both terrain goldens:
#   - fly-over: full 64 steps, exit_reason=1 (Completed)
#   - collision: truncated trace with all-zero tail, exit_reason=7
#     (SurfaceCollision) at the same step as the CPU golden
# plus batch isolation on the fly-over tile.
#
# Needs a real GPU: run outside the sandbox.
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steps="${1:-64}"
instances_list="${2:-1 8 32 128}"
scenario="${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json"
aero_table="${workspace_dir}/build/campaign_trajectory_golden_table.aero"
tile_flyover="${workspace_dir}/build/terrain_flat_990.tile"
tile_collision="${workspace_dir}/build/terrain_collide_999m5.tile"
golden_flyover="${workspace_dir}/golden/campaign_trajectory_golden_terrain.txt"
golden_collision="${workspace_dir}/golden/campaign_trajectory_golden_terrain_collision.txt"
bin="${workspace_dir}/build/bin/gpu_campaign"
mkdir -p "${workspace_dir}/logs"

# 1. CPU golden baselines (regenerates tiles, asserts bitwise reproducibility
#    and the geocentric goldens stay unchanged).
"${workspace_dir}/tools/build_campaign_trajectory_golden.sh" \
    | tee "${workspace_dir}/logs/terrain_golden.log"

# 2. The collision golden must show a mid-run termination.
collision_step=$(sed -n 's/^# surface_collision_step: \(-\{0,1\}[0-9]*\)$/\1/p' \
    "${golden_collision}")
if [ -z "${collision_step}" ] || [ "${collision_step}" -lt 1 ]; then
    echo "terrain verification: FAIL - collision golden did not terminate early " \
         "(step=${collision_step})" >&2
    exit 1
fi
flyover_step=$(sed -n 's/^# surface_collision_step: \(-\{0,1\}[0-9]*\)$/\1/p' \
    "${golden_flyover}")
if [ "${flyover_step}" != "-1" ]; then
    echo "terrain verification: FAIL - fly-over golden terminated early" >&2
    exit 1
fi
echo "terrain goldens: fly-over complete, collision at step ${collision_step}"

# 3. GPU fly-over acceptance: full trace, exit_reason=1.
dump="${workspace_dir}/logs/terrain_flyover_steps_${steps}.txt"
echo "== terrain fly-over (flat 990 m) =="
"${bin}" \
    --steps "${steps}" --instances 1 \
    --repeats 1 --warmup 0 \
    --scenario "${scenario}" \
    --aero-file "${aero_table}" \
    --terrain-file "${tile_flyover}" \
    --dump-trace "${dump}" \
    | tee "${workspace_dir}/logs/terrain_flyover_steps_${steps}.log"
grep -q "exit_reason=1 " "${workspace_dir}/logs/terrain_flyover_steps_${steps}.log" || {
    echo "terrain verification: FAIL - fly-over run did not complete" >&2
    exit 1
}
python3 "${workspace_dir}/tools/compare_golden_trace.py" \
    "${golden_flyover}" "${dump}"

# 4. GPU collision acceptance: truncated trace with zero tail, exit_reason=7
#    at the golden's step.
dump="${workspace_dir}/logs/terrain_collision_steps_${steps}.txt"
echo "== terrain surface collision (flat 999.5 m) =="
"${bin}" \
    --steps "${steps}" --instances 1 \
    --repeats 1 --warmup 0 \
    --scenario "${scenario}" \
    --aero-file "${aero_table}" \
    --terrain-file "${tile_collision}" \
    --dump-trace "${dump}" \
    | tee "${workspace_dir}/logs/terrain_collision_steps_${steps}.log"
grep -q "exit_reason=7 " "${workspace_dir}/logs/terrain_collision_steps_${steps}.log" || {
    echo "terrain verification: FAIL - collision run exit_reason != 7" >&2
    exit 1
}
python3 "${workspace_dir}/tools/compare_golden_trace.py" \
    --allow-zero-tail "${golden_collision}" "${dump}"

# 5. Batch isolation on the fly-over tile: instance 0 must match the golden
#    for every batch size.
for instances in ${instances_list}; do
    dump="${workspace_dir}/logs/terrain_${instances}_steps_${steps}.txt"
    echo "== instances=${instances} steps=${steps} (terrain fly-over) =="
    "${bin}" \
        --steps "${steps}" --instances "${instances}" \
        --repeats 1 --warmup 0 \
        --scenario "${scenario}" \
        --aero-file "${aero_table}" \
        --terrain-file "${tile_flyover}" \
        --dump-trace "${dump}" \
        | tee "${workspace_dir}/logs/terrain_${instances}_steps_${steps}.log"
    python3 "${workspace_dir}/tools/compare_golden_trace.py" \
        "${golden_flyover}" "${dump}"
done

echo "terrain-verification: PASS fly-over + collision + batch N=${instances_list}"
