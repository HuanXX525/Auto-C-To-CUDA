#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "${workspace_dir}/build"
objects=(
  "${workspace_dir}"/build/converted/common/{vec3,matrix3,quaternion,random,ring_buffer}.o
  "${workspace_dir}"/build/converted/environment/{missile_plant_6dof,sensor_noise,sensor_vector3,sensor_imu,sensor_accel,sensor_speed,sensor_seeker}.o
  "${workspace_dir}"/build/converted/flight_control/*.o
)
nvcc -std=c++14 -I "${workspace_dir}/staging/headers" \
  "${workspace_dir}/host_tests/campaign_chain_compare.cpp" "${objects[@]}" \
  -o "${workspace_dir}/build/campaign_chain_compare"
"${workspace_dir}/build/campaign_chain_compare"
