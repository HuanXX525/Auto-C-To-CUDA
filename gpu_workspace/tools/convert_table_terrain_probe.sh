#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${workspace_dir}/.." && pwd)"
translator="${repo_dir}/build/bin/translate.out"
mkdir -p "${workspace_dir}/converted/environment_hostonly" "${workspace_dir}/logs"

"${translator}" --scan-dir "${workspace_dir}/staging/environment_hostonly" \
    --output-dir "${workspace_dir}/converted/environment_hostonly" \
    --include "${workspace_dir}/staging/headers" \
    -rose:C99 >"${workspace_dir}/logs/table_terrain_convert.log" 2>&1

status=0
for source in "${workspace_dir}"/converted/environment_hostonly/*.cu; do
    name="$(basename "${source}" .cu)"
    object="${workspace_dir}/build/${name}_hostonly.o"
    if nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
        -I "${workspace_dir}/converted" "${source}" -o "${object}" \
        >"${workspace_dir}/logs/${name}_hostonly_nvcc.log" 2>&1; then
        printf '%s: nvcc-pass (device suitability still requires pointer/I-O audit)\n' "${name}"
    else
        printf '%s: host-only-or-blocked (see logs/%s_hostonly_nvcc.log)\n' \
            "${name}" "${name}"
        status=1
    fi
done

# Conversion/compilation failures are recorded for classification, not hidden.
exit 0
