#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "${workspace_dir}/build" "${workspace_dir}/logs"
status=0

for source in "${workspace_dir}"/converted/common/*.cu \
              "${workspace_dir}"/converted/environment/*.cu \
              "${workspace_dir}"/converted/flight_control/*.cu; do
    name="$(basename "${source}" .cu)"
    if [[ "${name}" == "aero_surrogate" ]]; then
        printf '%s: host-only (skipped from device build)\n' "${name}"
        continue
    fi
    object="${workspace_dir}/build/${name}.o"
    if nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
        -I "${workspace_dir}/converted" "${source}" -o "${object}" \
        >"${workspace_dir}/logs/${name}_nvcc.log" 2>&1; then
        printf '%s: nvcc-pass\n' "${name}"
    else
        printf '%s: nvcc-fail (see logs/%s_nvcc.log)\n' "${name}" "${name}"
        status=1
    fi
done

exit "${status}"
