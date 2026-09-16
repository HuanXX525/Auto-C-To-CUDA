/** @file safety_monitor.c
 *  @brief 飞控安全监视器实现。
 */
#include "fc/safety_monitor.h"
#include "fc/fc_health.h"
#include <math.h>
#include <string.h>
/** @brief 判断传感器帧中的协议公开测量是否均为有限数。 */
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

C2CUDA_HOST_DEVICE static int sensor_frame_measurements_finite(const SensorFrame *sensor)
{
  return sensor != 0 && __builtin_isfinite(sensor -> sim_time) && __builtin_isfinite(sensor -> dt) && vec3_isfinite(sensor -> missile_vel_ecef_meas) && vec3_isfinite(sensor -> missile_accel_ecef_meas) && vec3_isfinite(sensor -> missile_gyro_b_meas) && __builtin_isfinite(sensor -> missile_lat_rad_meas) && __builtin_isfinite(sensor -> missile_lon_rad_meas) && __builtin_isfinite(sensor -> missile_height_m_meas) && __builtin_isfinite(sensor -> missile_height_agl_m_meas) && __builtin_isfinite(sensor -> target_range_meas) && vec3_isfinite(sensor -> target_los_unit_ecef_meas) && vec3_isfinite(sensor -> target_los_rate_ecef_meas) && __builtin_isfinite(sensor -> target_closing_velocity_meas);
}

SimStatus safety_monitor_init(SafetyMonitor *monitor,const FcSafetyConfig *config)
{
  if (monitor == 0 || config == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (!__builtin_isfinite(config -> sensor_timeout_s) || !__builtin_isfinite(config -> command_hold_s) || config -> sensor_timeout_s < 0.0 || config -> command_hold_s < 0.0) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  (void )(memset(monitor,0,sizeof(( *monitor))));
  monitor -> config =  *config;
  monitor -> config . max_consecutive_bad_frames = (monitor -> config . max_consecutive_bad_frames == 0u?3u : monitor -> config . max_consecutive_bad_frames);
  return SIM_OK;
}

SimStatus safety_monitor_check_sensor(SafetyMonitor *monitor,const SensorFrame *sensor,int have_last_seq,uint32_t last_seq,int have_last_sensor_time,double last_sensor_time,SafetyAssessment *out)
{
  uint32_t flags = 0u;
  int bad_frame = 0;
  if (monitor == 0 || sensor == 0 || out == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  (void )(memset(out,0,sizeof(( *out))));
  out -> accept_frame = 1;
  if (monitor -> config . reject_nan != 0 && !sensor_frame_measurements_finite(sensor)) {
    flags |= 0x80000000U;
    bad_frame = 1;
    out -> accept_frame = 0;
    out -> request_hold = 1;
  }
  if (monitor -> config . reject_old_seq != 0 && have_last_seq != 0 && sensor -> seq <= last_seq) {
    flags |= 0x00000001U;
    bad_frame = 1;
    out -> accept_frame = 0;
    out -> request_hold = 1;
  }
  if (have_last_sensor_time != 0 && sensor -> sim_time - last_sensor_time > monitor -> config . sensor_timeout_s) {
    flags |= 0x00000002U;
    out -> request_hold = 1;
    out -> degraded = 1;
  }
  if ((sensor -> sensor_valid_flags & 0x00000001U) == 0u) {
    flags |= 0x00000004U;
    out -> request_hold = 1;
  }
  if ((sensor -> sensor_valid_flags & 0x00000001U) != 0u && (sensor -> target_range_meas <= 0.0 || sensor -> target_closing_velocity_meas <= 0.0)) {
    flags |= 0x00000008U;
    out -> request_hold = 1;
  }
  if (bad_frame != 0 || out -> request_hold != 0) {
    monitor -> consecutive_bad_frames += 1u;
  }
   else {
    monitor -> consecutive_bad_frames = 0u;
  }
  out -> fault = (monitor -> consecutive_bad_frames > monitor -> config . max_consecutive_bad_frames && (flags & 0x80000000U) != 0u?1 : out -> fault);
  out -> status_flags = flags;
  monitor -> status_flags = flags;
  return SIM_OK;
}
