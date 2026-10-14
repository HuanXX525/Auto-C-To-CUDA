/** @file navigation.c
 *  @brief 传感器帧到飞控导航状态的转换实现。
 */
#include "fc/navigation.h"
#include <math.h>
#include <string.h>
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

SimStatus navigation_update_from_sensor(const SensorFrame *sensor,NavState *out)
{
  if (sensor == 0 || out == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (!__builtin_isfinite(sensor -> sim_time) || !__builtin_isfinite(sensor -> target_range_meas) || !__builtin_isfinite(sensor -> target_closing_velocity_meas) || !vec3_isfinite(sensor -> missile_vel_ecef_meas) || !vec3_isfinite(sensor -> missile_accel_ecef_meas) || !vec3_isfinite(sensor -> missile_gyro_b_meas) || !vec3_isfinite(sensor -> target_los_unit_ecef_meas) || !vec3_isfinite(sensor -> target_los_rate_ecef_meas)) {
    return SIM_ERR_NUMERIC;
  }
  (void )(memset(out,0,sizeof(( *out))));
  out -> time = sensor -> sim_time;
  if ((sensor -> sensor_valid_flags & 0x00000008U) != 0u || (sensor -> sensor_valid_flags & 0x00000004U) != 0u || (sensor -> sensor_valid_flags & 0x00000002U) != 0u) {
    out -> missile_vel_ecef_est = sensor -> missile_vel_ecef_meas;
    out -> missile_accel_ecef_est = sensor -> missile_accel_ecef_meas;
    out -> omega_b_est = sensor -> missile_gyro_b_meas;
    out -> valid_flags |= 0x00000001U;
  }
  if ((sensor -> sensor_valid_flags & 0x00000010U) != 0u) {
    if (!__builtin_isfinite(sensor -> missile_lat_rad_meas) || !__builtin_isfinite(sensor -> missile_lon_rad_meas) || !__builtin_isfinite(sensor -> missile_height_m_meas) || !__builtin_isfinite(sensor -> missile_height_agl_m_meas)) {
      return SIM_ERR_NUMERIC;
    }
    out -> lat_rad = sensor -> missile_lat_rad_meas;
    out -> lon_rad = sensor -> missile_lon_rad_meas;
    out -> height_m = sensor -> missile_height_m_meas;
    out -> height_agl_m = sensor -> missile_height_agl_m_meas;
    out -> valid_flags |= 0x00000004U;
  }
  if ((sensor -> sensor_valid_flags & 0x00000001U) != 0u) {
    out -> range = sensor -> target_range_meas;
    out -> los_unit_ecef = sensor -> target_los_unit_ecef_meas;
    out -> los_rate_ecef = sensor -> target_los_rate_ecef_meas;
    out -> closing_velocity = sensor -> target_closing_velocity_meas;
    out -> valid_flags |= 0x00000002U;
  }
  return SIM_OK;
}

int navigation_has_guidance_solution(const NavState *nav)
{
  return nav != 0 && (nav -> valid_flags & 0x00000002U) != 0u && __builtin_isfinite(nav -> range) && __builtin_isfinite(nav -> closing_velocity) && nav -> range > 0.0 && nav -> closing_velocity > 0.0 && vec3_isfinite(nav -> los_unit_ecef) && vec3_isfinite(nav -> los_rate_ecef);
}
