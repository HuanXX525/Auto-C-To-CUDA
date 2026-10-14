#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steps="${1:-8}"
repeats="${2:-10}"
warmup="${3:-2}"
mkdir -p "${workspace_dir}/logs"

for instances in 1 2 8 32; do
    log_file="${workspace_dir}/logs/multi_instances_${instances}_steps_${steps}.log"
    echo "== instances=${instances} steps=${steps} =="
    "${workspace_dir}/build/bin/gpu_campaign" \
        --steps "${steps}" --instances "${instances}" --repeats "${repeats}" \
        --warmup "${warmup}" | tee "${log_file}"
done

python3 "${workspace_dir}/tools/check_batch_isolation.py" "${steps}"
