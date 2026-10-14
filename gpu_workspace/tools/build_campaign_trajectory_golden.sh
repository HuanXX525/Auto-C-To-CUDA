#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
headers_dir="${workspace_dir}/staging/headers"
build_dir="${workspace_dir}/build"
golden_dir="${workspace_dir}/golden"
mkdir -p "${build_dir}" "${golden_dir}"

# Staging sources compiled as C++ (same pattern as the force probe) so the
# golden chain runs the original environment/flight-control implementation.
staging_sources=(
    common/vec3 common/matrix3 common/quaternion common/random common/ring_buffer
    environment/aero_model environment/aero_surrogate environment/atmosphere_model
    environment/missile_plant_6dof environment/sensor_noise environment/sensor_vector3
    environment/sensor_imu environment/sensor_accel environment/sensor_speed
    environment/sensor_seeker
    environment/fault_injection environment/fault_injection_loader
    environment/earth_model environment/geo_coordinate
    flight_control/autopilot flight_control/command_manager flight_control/estimator
    flight_control/fc_health flight_control/fc_modes flight_control/fc_scheduler
    flight_control/fc_state flight_control/guidance_manager flight_control/guidance_png
    flight_control/navigation flight_control/safety_monitor
    environment_hostonly/aero_database
)
objects=()
for source in "${staging_sources[@]}"; do
    object="${build_dir}/golden_$(basename "${source}").o"
    g++ -std=c++14 -x c++ -I "${headers_dir}" -c \
        "${workspace_dir}/staging/${source}.c" -o "${object}"
    objects+=("${object}")
done

g++ -std=c++14 -x c++ -I "${headers_dir}" -c \
    "${workspace_dir}/../external/Aircraft-Simu/common/src/crc32.c" \
    -o "${build_dir}/golden_crc32.o"
objects+=("${build_dir}/golden_crc32.o")

# Scenario mapper: original config.c reader (C) plus the C++ mapper.
gcc -std=c11 -I "${headers_dir}" -c \
    "${workspace_dir}/../external/Aircraft-Simu/common/src/config.c" \
    -o "${build_dir}/golden_config.o"
g++ -std=c++14 -I "${headers_dir}" -I "${workspace_dir}/gpu_campaign/include" -c \
    "${workspace_dir}/gpu_campaign/src/host_scenario_mapper.cpp" \
    -o "${build_dir}/golden_host_scenario_mapper.o"

# The terrain golden loads the tile via pack_terrain_tile_file, which uses
# the original map_tile.c reader (compiled as C, like config.c).
gcc -std=c11 -I "${headers_dir}" -c \
    "${workspace_dir}/../external/Aircraft-Simu/environment_sim/src/map_tile.c" \
    -o "${build_dir}/golden_map_tile.o"
gcc -std=c11 -I "${headers_dir}" -c \
    "${workspace_dir}/../external/Aircraft-Simu/common/src/crc32.c" \
    -o "${build_dir}/golden_crc32_c.o"
g++ -std=c++14 -I "${headers_dir}" -I "${workspace_dir}/gpu_campaign/include" -c \
    "${workspace_dir}/gpu_campaign/src/host_environment_packer.cpp" \
    -o "${build_dir}/golden_host_environment_packer.o"

g++ -std=c++14 -I "${headers_dir}" -I "${workspace_dir}/gpu_campaign/include" -c \
    "${workspace_dir}/host_tests/campaign_trajectory_golden.cpp" \
    -o "${build_dir}/golden_campaign_trajectory.o"

g++ "${build_dir}/golden_campaign_trajectory.o" \
    "${build_dir}/golden_host_scenario_mapper.o" \
    "${build_dir}/golden_host_environment_packer.o" \
    "${objects[@]}" "${build_dir}/golden_config.o" \
    "${build_dir}/golden_map_tile.o" "${build_dir}/golden_crc32_c.o" -lm \
    -o "${build_dir}/campaign_trajectory_golden"

"${build_dir}/campaign_trajectory_golden" \
    "${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json" \
    "${build_dir}/campaign_trajectory_golden_table.aero" \
    "${golden_dir}/campaign_trajectory_golden.txt" quiet

# planA Phase 4 items 1/2: same chain with the scenario sensor configs
# (noise, seeker delay, dropout probability) enabled.
"${build_dir}/campaign_trajectory_golden" \
    "${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json" \
    "${build_dir}/campaign_trajectory_golden_table.aero" \
    "${golden_dir}/campaign_trajectory_golden_noise.txt" sensors

# planA Phase 4 item 3: sensor noise plus a four-fault verification script
# (DROPOUT / RAMP_BIAS / HOLD_VALUE / BIAS on sensor targets, all inside the
# 64-step window). Actuator and communication faults are rejected by the
# GPU scope guard.
cat > "${build_dir}/faults_verification.json" <<'JSONEOF'
{
  "schema_version": 1,
  "faults": [
    {
      "id": "seeker_dropout",
      "enabled": true,
      "start_time_s": 0.20,
      "duration_s": 0.10,
      "target": "sensor.seeker",
      "type": "DROPOUT"
    },
    {
      "id": "gyro_ramp_bias",
      "enabled": true,
      "start_time_s": 0.30,
      "duration_s": 0.10,
      "ramp_in_s": 0.05,
      "target": "sensor.imu.gyro",
      "type": "RAMP_BIAS",
      "value_xyz": [0.01, 0.0, 0.0]
    },
    {
      "id": "speed_hold",
      "enabled": true,
      "start_time_s": 0.40,
      "duration_s": 0.10,
      "target": "sensor.speed",
      "type": "HOLD_VALUE",
      "value_xyz": [5.0, -5.0, 0.0]
    },
    {
      "id": "los_rate_bias",
      "enabled": true,
      "start_time_s": 0.50,
      "duration_s": 0.20,
      "target": "sensor.seeker.los_rate",
      "type": "BIAS",
      "value_xyz": [0.0, 0.0, 0.001]
    }
  ]
}
JSONEOF
"${build_dir}/campaign_trajectory_golden" \
    "${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json" \
    "${build_dir}/campaign_trajectory_golden_table.aero" \
    "${golden_dir}/campaign_trajectory_golden_fault.txt" sensors \
    "${build_dir}/faults_verification.json"

# planA Phase 4 item 4: terrain dynamics. Two flat tiles around the WGS-84
# scene origin LLA(30deg, 120deg): 990 m (fly-over, full 64 steps) and
# 999.5 m (surface collision mid-run).
g++ -std=c++14 -I "${headers_dir}" -c \
    "${workspace_dir}/host_tests/terrain_tile_generator.cpp" \
    -o "${build_dir}/golden_terrain_tile_generator.o"
g++ "${build_dir}/golden_terrain_tile_generator.o" \
    "${build_dir}/golden_map_tile.o" "${build_dir}/golden_crc32_c.o" -lm \
    -o "${build_dir}/terrain_tile_generator"
"${build_dir}/terrain_tile_generator" \
    "${build_dir}/terrain_flat_990.tile" 990.0
"${build_dir}/terrain_tile_generator" \
    "${build_dir}/terrain_collide_999m5.tile" 999.5
"${build_dir}/campaign_trajectory_golden" \
    "${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json" \
    "${build_dir}/campaign_trajectory_golden_table.aero" \
    "${golden_dir}/campaign_trajectory_golden_terrain.txt" sensors - \
    "${build_dir}/terrain_flat_990.tile"
"${build_dir}/campaign_trajectory_golden" \
    "${workspace_dir}/../external/Aircraft-Simu/configs/baseline/scenario.json" \
    "${build_dir}/campaign_trajectory_golden_table.aero" \
    "${golden_dir}/campaign_trajectory_golden_terrain_collision.txt" sensors - \
    "${build_dir}/terrain_collide_999m5.tile"
