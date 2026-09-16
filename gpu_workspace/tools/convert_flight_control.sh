#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${workspace_dir}/.." && pwd)"
translator="${repo_dir}/build/bin/translate.out"

mkdir -p "${workspace_dir}/converted/flight_control" "${workspace_dir}/logs"
"${translator}" --scan-dir "${workspace_dir}/staging/flight_control" \
    --output-dir "${workspace_dir}/converted/flight_control" \
    --include "${workspace_dir}/staging/headers" \
    -rose:C99 >"${workspace_dir}/logs/fc_convert.log" 2>&1
python3 "${workspace_dir}/tools/sanitize_fc_generated.py"
