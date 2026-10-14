#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${workspace_dir}/.." && pwd)"
case_dir="${workspace_dir}/build/m4_fault_los"
generator="${workspace_dir}/build/m4_terrain_tile_generator"
mkdir -p "${workspace_dir}/build"

gcc -std=c11 -I "${workspace_dir}/staging/headers" -c \
    "${repo_dir}/external/Aircraft-Simu/environment_sim/src/map_tile.c" \
    -o "${workspace_dir}/build/m4_map_tile.o"
gcc -std=c11 -I "${workspace_dir}/staging/headers" -c \
    "${repo_dir}/external/Aircraft-Simu/common/src/crc32.c" \
    -o "${workspace_dir}/build/m4_crc32.o"
g++ -std=c++14 -I "${workspace_dir}/staging/headers" \
    "${workspace_dir}/host_tests/terrain_tile_generator.cpp" \
    "${workspace_dir}/build/m4_map_tile.o" "${workspace_dir}/build/m4_crc32.o" \
    -lm -o "${generator}"

"${workspace_dir}/tools/build_cpu_baseline.sh"
make -C "${workspace_dir}" -j2
python3 "${workspace_dir}/tools/prepare_m4_cases.py" \
    --repo "${repo_dir}" --output "${case_dir}" --generator "${generator}"

cases=(los_clear missing_flat los_index_clear los_index_occluded los_borderline \
       sensor_faults actuator_faults communication_delay communication_reorder \
       fault_overlap_recovery)
for case_name in "${cases[@]}"; do
    runtime="${case_dir}/runtime_${case_name}.json"
    cpu_output="${case_dir}/cpu_${case_name}"
    gpu_output="${case_dir}/gpu_${case_name}"
    "${workspace_dir}/build/cpu_baseline/instance_manager" --runtime "${runtime}"
    "${workspace_dir}/build/bin/gpu_campaign" \
        --config "${runtime}" --output-dir "${gpu_output}" \
        --trace-mode full --chunk-steps 17 --mapping packed --lanes-per-block 4
    python3 "${repo_dir}/compare_runs.py" \
        "${cpu_output}" "${gpu_output}" --rtol 1e-10 --atol 1e-12
    python3 "${workspace_dir}/tools/check_m4_packets.py" \
        --case "${case_name}" --cpu "${cpu_output}" --gpu "${gpu_output}"
done

set +e
"${workspace_dir}/build/cpu_baseline/instance_manager" \
    --runtime "${case_dir}/runtime_missing_error.json" >/dev/null 2>&1
cpu_status=$?
"${workspace_dir}/build/bin/gpu_campaign" \
    --config "${case_dir}/runtime_missing_error.json" \
    --output-dir "${case_dir}/gpu_missing_error" >/dev/null 2>&1
gpu_status=$?
set -e
error_reason=$(python3 -c "import json; print(json.load(open('${case_dir}/gpu_missing_error/instance_0000/summary.json'))['exit_reason'])")
if [[ "${error_reason}" != "terrain_initialization_failed" ]]; then
    echo "m4_missing_error: FAIL CPU=${cpu_status} GPU=${gpu_status} reason=${error_reason}" >&2
    exit 1
fi
echo "m4_missing_error: PASS CPU=${cpu_status} GPU=${gpu_status} reason=${error_reason}"
python3 "${workspace_dir}/tools/run_m4_batch_isolation.py" \
    --workspace "${workspace_dir}" \
    --source-runtime "${case_dir}/runtime_fault_overlap_recovery.json" \
    --output "${case_dir}/batch_isolation"
echo "m4_verification: PASS LOS terrain order and all fault classes"
