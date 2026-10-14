#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${workspace_dir}"

# Generate deterministic binary fixtures, then exercise the real campaign
# upload path. The timing line is intentionally not compared.
tools/build_host_environment_packer_probe.sh >/dev/null
aero_file="${workspace_dir}/build/adapter_probe.aero"
terrain_file="${workspace_dir}/build/adapter_probe.tile"

single_output="$(build/bin/gpu_campaign --steps 8 \
    --aero-file "${aero_file}" --terrain-file "${terrain_file}")"
printf '%s\n' "${single_output}"
grep -q 'final_x=0.799988597849' <<<"${single_output}"
grep -q 'environment_flags=0x00000000' <<<"${single_output}"

batch_output="$(build/bin/gpu_campaign --steps 8 --instances 2 \
    --aero-file "${aero_file}" --terrain-file "${terrain_file}")"
printf '%s\n' "${batch_output}"
test "$(grep -c 'environment_flags=0x00000000' <<<"${batch_output}")" -ge 2
printf 'environment_campaign_probe: PASS\n'
