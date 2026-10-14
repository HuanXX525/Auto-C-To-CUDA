/** @file autopilot.c
 *  @brief 自动驾驶仪命令转换实现。
 */
#include "fc/autopilot.h"
#include <math.h>
#include <string.h>
#define FC_AUTOPILOT_G0_MPS2 9.80665
/** @brief 返回限制到闭区间内的标量值。 */
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

C2CUDA_HOST_DEVICE static double clamp_scalar(double value,double minimum,double maximum)
{
  return value < minimum?minimum : ((value > maximum?maximum : value));
}
/** @brief 按绝对值上限限制一个三维向量的各个分量。 */

C2CUDA_HOST_DEVICE static Vec3 clamp_vec3_components(Vec3 value,double limit)
{
  if (!__builtin_isfinite(limit) || limit < 0.0) {
    return value;
  }
  return vec3_make((clamp_scalar(value . x,-limit,limit)),(clamp_scalar(value . y,-limit,limit)),(clamp_scalar(value . z,-limit,limit)));
}
/** @brief 使用导航速度和大地坐标建立速度坐标系横向/法向轴。 */

C2CUDA_HOST_DEVICE static SimStatus build_velocity_axes(const NavState *nav,Vec3 *right_ecef,Vec3 *normal_ecef)
{
  Vec3 forward;
  Vec3 local_up;
  Vec3 right;
  Vec3 normal;
  double cos_lat;
  SimStatus status;
  if (nav == 0 || right_ecef == 0 || normal_ecef == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  status = vec3_normalize(nav -> missile_vel_ecef_est,&forward);
  if (status != SIM_OK) {
    return status;
  }
  if ((nav -> valid_flags & 0x00000004U) != 0u && __builtin_isfinite(nav -> lat_rad) && __builtin_isfinite(nav -> lon_rad)) {
    cos_lat = cos(nav -> lat_rad);
    local_up = vec3_make(cos_lat * cos(nav -> lon_rad),cos_lat * sin(nav -> lon_rad),(sin(nav -> lat_rad)));
  }
   else {
    local_up = vec3_make(0.0,0.0,1.0);
  }
  right = vec3_cross(forward,local_up);
  if (vec3_norm(right) <= 1.0e-9) {
    right = vec3_cross(forward,(vec3_make(0.0,1.0,0.0)));
  }
  status = vec3_normalize(right,&right);
  if (status != SIM_OK) {
    return status;
  }
  normal = vec3_cross(right,forward);
  status = vec3_normalize(normal,&normal);
  if (status != SIM_OK) {
    return status;
  }
   *right_ecef = right;
   *normal_ecef = normal;
  return SIM_OK;
}
/** @brief 校验自动驾驶仪配置。 */

C2CUDA_HOST_DEVICE static SimStatus autopilot_config_validate(const AutopilotConfig *config)
{
  if (config == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (!__builtin_isfinite(config -> max_attitude_cmd_rad) || !__builtin_isfinite(config -> max_body_rate_cmd_radps) || !__builtin_isfinite(config -> attitude_time_constant_s) || !__builtin_isfinite(config -> gyro_damping_gain) || !__builtin_isfinite(config -> fin_accel_effectiveness_mps2_per_rad) || !__builtin_isfinite(config -> max_fin_deflection_rad) || config -> max_attitude_cmd_rad < 0.0 || config -> max_body_rate_cmd_radps < 0.0 || config -> attitude_time_constant_s <= 0.0 || config -> gyro_damping_gain < 0.0 || config -> fin_accel_effectiveness_mps2_per_rad < 0.0 || config -> max_fin_deflection_rad < 0.0) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  return SIM_OK;
}

SimStatus autopilot_init(Autopilot *autopilot,const AutopilotConfig *config)
{
  AutopilotConfig effective_config;
  SimStatus status;
  if (autopilot == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (config == 0) {
    effective_config . enable_attitude_loop = 1;
    effective_config . enable_control_allocation = 1;
    effective_config . max_attitude_cmd_rad = 0.35;
    effective_config . max_body_rate_cmd_radps = 1.0;
    effective_config . attitude_time_constant_s = 0.25;
    effective_config . gyro_damping_gain = 0.2;
    effective_config . fin_accel_effectiveness_mps2_per_rad = 150.0;
    effective_config . max_fin_deflection_rad = 0.35;
  }
   else {
    effective_config =  *config;
  }
  status = autopilot_config_validate((&effective_config));
  if (status != SIM_OK) {
    return status;
  }
  (void )(memset(autopilot,0,sizeof(( *autopilot))));
  autopilot -> config = effective_config;
  return SIM_OK;
}

void autopilot_zero_command(AutopilotCommand *out)
{
  if (out != 0) {
    (void )(memset(out,0,sizeof(( *out))));
  }
}

SimStatus autopilot_update(Autopilot *autopilot,const NavState *nav,const GuidancePngOutput *guidance,AutopilotCommand *out)
{
  Vec3 right_ecef;
  Vec3 normal_ecef;
  double lateral_right_mps2;
  double lateral_normal_mps2;
  double pitch_cmd_rad;
  double yaw_cmd_rad;
  double pitch_rate_cmd_radps;
  double yaw_rate_cmd_radps;
  if (autopilot == 0 || guidance == 0 || out == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (!vec3_isfinite(guidance -> accel_cmd_ecef)) {
    autopilot -> rejected_count += 1u;
    return SIM_ERR_NUMERIC;
  }
  autopilot_zero_command(out);
  out -> accel_cmd_ecef = guidance -> accel_cmd_ecef;
  if (autopilot -> config . enable_attitude_loop != 0) {
    const SimStatus axes_status = build_velocity_axes(nav,&right_ecef,&normal_ecef);
    if (axes_status != SIM_OK) {
      autopilot -> rejected_count += 1u;
      return axes_status;
    }
    lateral_right_mps2 = vec3_dot(guidance -> accel_cmd_ecef,right_ecef);
    lateral_normal_mps2 = vec3_dot(guidance -> accel_cmd_ecef,normal_ecef);
    pitch_cmd_rad = atan2(lateral_normal_mps2,9.80665);
    yaw_cmd_rad = atan2(lateral_right_mps2,9.80665);
    out -> attitude_cmd = clamp_vec3_components((vec3_make(0.0,pitch_cmd_rad,yaw_cmd_rad)),autopilot -> config . max_attitude_cmd_rad);
    pitch_rate_cmd_radps = out -> attitude_cmd . y / autopilot -> config . attitude_time_constant_s - autopilot -> config . gyro_damping_gain * nav -> omega_b_est . y;
    yaw_rate_cmd_radps = out -> attitude_cmd . z / autopilot -> config . attitude_time_constant_s - autopilot -> config . gyro_damping_gain * nav -> omega_b_est . z;
    out -> body_rate_cmd = clamp_vec3_components((vec3_make(0.0,pitch_rate_cmd_radps,yaw_rate_cmd_radps)),autopilot -> config . max_body_rate_cmd_radps);
    if (autopilot -> config . enable_control_allocation != 0 && autopilot -> config . fin_accel_effectiveness_mps2_per_rad > 0.0) {
      out -> actuator_cmd[0] = clamp_scalar(lateral_normal_mps2 / autopilot -> config . fin_accel_effectiveness_mps2_per_rad,-autopilot -> config . max_fin_deflection_rad,autopilot -> config . max_fin_deflection_rad);
      out -> actuator_cmd[1] = clamp_scalar(lateral_right_mps2 / autopilot -> config . fin_accel_effectiveness_mps2_per_rad,-autopilot -> config . max_fin_deflection_rad,autopilot -> config . max_fin_deflection_rad);
    }
  }
  autopilot -> accepted_count += 1u;
  return SIM_OK;
}
