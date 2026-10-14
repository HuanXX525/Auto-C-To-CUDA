#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${workspace_dir}/.." && pwd)"
translator="${repo_dir}/build/bin/translate.out"

if [[ ! -x "${translator}" ]]; then
    echo "translator not found: ${translator}" >&2
    exit 1
fi

mkdir -p "${workspace_dir}/converted/common"
"${translator}" --scan-dir "${workspace_dir}/staging/common" \
    --output-dir "${workspace_dir}/converted/common" \
    --include "${workspace_dir}/staging/headers" \
    --exclude generated \
    -rose:C99

python3 "${workspace_dir}/tools/sanitize_minimal_generated.py"
