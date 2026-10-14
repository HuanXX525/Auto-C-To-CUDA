#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
steps="${1:-8}"
instances="${2:-32}"
repeats="${3:-20}"
warmup="${4:-2}"
mkdir -p "${workspace_dir}/logs"

for threads in 1 32; do
    log_file="${workspace_dir}/logs/mapping_threads_${threads}_instances_${instances}_steps_${steps}.log"
    echo "== threads_per_instance=${threads} instances=${instances} steps=${steps} repeats=${repeats} warmup=${warmup} =="
    "${workspace_dir}/build/bin/gpu_campaign" \
        --steps "${steps}" --instances "${instances}" \
        --threads-per-instance "${threads}" --repeats "${repeats}" --warmup "${warmup}" \
        | tee "${log_file}"
done
