#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
instances="${1:-32}"
repeats="${2:-10}"
warmup="${3:-2}"
shift 3 || true
if [[ "$#" -eq 0 ]]; then
    set -- 8 128 512 2048
fi

mkdir -p "${workspace_dir}/logs"
for steps in "$@"; do
    log_file="${workspace_dir}/logs/scaling_instances_${instances}_steps_${steps}.log"
    echo "== instances=${instances} steps=${steps} repeats=${repeats} warmup=${warmup} =="
    "${workspace_dir}/build/bin/gpu_campaign" \
        --steps "${steps}" --instances "${instances}" \
        --threads-per-instance 1 --repeats "${repeats}" --warmup "${warmup}" \
        | tee "${log_file}"
done
