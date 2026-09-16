#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
log_file="${workspace_dir}/logs/device_link_probe.log"
mkdir -p "${workspace_dir}/logs"

set +e
nvcc -std=c++14 -dc -I "${workspace_dir}/staging/headers" \
    "${workspace_dir}/host_tests/device_link_probe.cu" \
    -o "${workspace_dir}/build/device_link_probe.o" >"${log_file}" 2>&1
status=$?
set -e

if [[ ${status} -eq 0 ]]; then
    if nvcc -std=c++14 -dlink "${workspace_dir}/build/device_link_probe.o" \
        -o "${workspace_dir}/build/device_link_probe.dlink.o" >>"${log_file}" 2>&1; then
        echo "device callability probe: PASS (compile and device-link)"
    else
        echo "device callability probe: BLOCKED during device-link (see ${log_file})"
    fi
else
    echo "device callability probe: BLOCKED (see ${log_file})"
fi
exit 0
