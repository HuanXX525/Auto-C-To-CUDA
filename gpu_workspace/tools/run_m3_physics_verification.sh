#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${workspace_dir}/.." && pwd)"
case_dir="${workspace_dir}/build/m3_physics"

"${workspace_dir}/tools/build_cpu_baseline.sh"
make -C "${workspace_dir}" -j2
python3 "${workspace_dir}/tools/prepare_m3_physics_cases.py" \
    --repo "${repo_dir}" --output "${case_dir}"

for case_name in actuator gravity propulsion; do
    runtime="${case_dir}/runtime_${case_name}.json"
    cpu_output="${case_dir}/cpu_${case_name}"
    gpu_output="${case_dir}/gpu_${case_name}"
    "${workspace_dir}/build/cpu_baseline/instance_manager" \
        --runtime "${runtime}"
    "${workspace_dir}/build/bin/gpu_campaign" \
        --config "${runtime}" \
        --output-dir "${gpu_output}" \
        --trace-mode full \
        --chunk-steps 64 \
        --mapping packed \
        --lanes-per-block 4
    python3 "${repo_dir}/compare_runs.py" \
        "${cpu_output}" "${gpu_output}" \
        --rtol 1e-10 --atol 1e-12 --verbose
    for result in "${cpu_output}" "${gpu_output}"; do
        python3 "${workspace_dir}/tools/check_m3_physics_golden.py" \
            --golden "${workspace_dir}/golden/m3_physics_golden.json" \
            --case "${case_name}" --result "${result}"
    done
done

echo "m3_physics_verification: PASS actuator gravity propulsion"
