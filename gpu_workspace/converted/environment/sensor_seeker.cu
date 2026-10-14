/** @file sensor_seeker.c
 *  @brief 导引头弹目相对测量、误差、采样和延迟实现。
 */
#include "env/sensor_seeker.h"
#include <math.h>
#include <string.h>
/** @brief 将延迟秒数转换为不短于配置值的整数仿真步。 */
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

C2CUDA_HOST_DEVICE static SimStatus seeker_delay_steps(double delay_s,double simulation_dt_s,size_t *delay_steps)
{
  double steps;
  if (delay_steps == 0 || !__builtin_isfinite(delay_s) || delay_s < 0.0 || !__builtin_isfinite(simulation_dt_s) || simulation_dt_s <= 0.0) {
    return SIM_ERR_INVALID_ARG;
  }
  steps = ceil(delay_s / simulation_dt_s - 1.0e-12);
  if (!__builtin_isfinite(steps) || steps < 0.0 || steps > ((double )1024u)) {
    return SIM_ERR_OUT_OF_RANGE;
  }
   *delay_steps = ((size_t )steps);
  return SIM_OK;
}
/** @brief 返回向量指定分量。 */

static double vector_component(Vec3 value,size_t index)
{
  if (index == 0u) {
    return value . x;
  }
  return index == 1u?value . y : value . z;
}
/** @brief 对标量应用误差，但由导引头统一处理整帧丢包。 */

C2CUDA_HOST_DEVICE static SimStatus apply_scalar_noise(const SensorNoiseConfig *config,SensorNoiseState *state,SimRandom *random,double truth,double dt_s,double *measurement)
{
  SensorNoiseConfig component_config;
  int dropped = 0;
  if (config == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  component_config =  *config;
  component_config . dropout_probability = 0.0;
  return sensor_noise_apply((&component_config),state,random,truth,dt_s,measurement,&dropped);
}
/** @brief 校验采样、延迟、整帧丢包和全部测量通道误差参数。 */

SimStatus sensor_seeker_validate(const SeekerSensorConfig *config,double simulation_dt_s)
{
  size_t delay_steps;
  size_t index;
  SimStatus status;
  if (config == 0 || !__builtin_isfinite(config -> sample_period_s) || config -> sample_period_s <= 0.0 || !__builtin_isfinite(config -> dropout_probability) || config -> dropout_probability < 0.0 || config -> dropout_probability > 1.0) {
    return SIM_ERR_INVALID_ARG;
  }
  status = seeker_delay_steps(config -> delay_s,simulation_dt_s,&delay_steps);
  if (status != SIM_OK) {
    return status;
  }
  status = sensor_noise_validate(&config -> range);
  if (status != SIM_OK) {
    return status;
  }
  status = sensor_noise_validate(&config -> closing_velocity);
  if (status != SIM_OK) {
    return status;
  }
  for (index = 0u; index < 3u; ++index) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;{
      const SensorNoiseConfig *config__12_inline_7 = &config -> los_unit_axis[index];
      if (config__12_inline_7 == 0) {{
          status = SIM_ERR_INVALID_ARG;
          goto rose_inline_end__13;
        }
      }
      if (!__builtin_isfinite(config__12_inline_7 -> bias) || !__builtin_isfinite(config__12_inline_7 -> white_noise_std) || !__builtin_isfinite(config__12_inline_7 -> random_walk_std) || !__builtin_isfinite(config__12_inline_7 -> min_value) || !__builtin_isfinite(config__12_inline_7 -> max_value) || !__builtin_isfinite(config__12_inline_7 -> resolution) || !__builtin_isfinite(config__12_inline_7 -> dropout_probability) || config__12_inline_7 -> white_noise_std < 0.0 || config__12_inline_7 -> random_walk_std < 0.0 || config__12_inline_7 -> min_value > config__12_inline_7 -> max_value || config__12_inline_7 -> resolution < 0.0 || config__12_inline_7 -> dropout_probability < 0.0 || config__12_inline_7 -> dropout_probability > 1.0) {{
          status = SIM_ERR_OUT_OF_RANGE;
          goto rose_inline_end__13;
        }
      }
{
        status = SIM_OK;
        goto rose_inline_end__13;
      }
      rose_inline_end__13:
      ;
    }
    if (status == SIM_OK) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;{
        const SensorNoiseConfig *config__15_inline_8 = &config -> los_rate_axis[index];
        if (config__15_inline_8 == 0) {{
            status = SIM_ERR_INVALID_ARG;
            goto rose_inline_end__16;
          }
        }
        if (!__builtin_isfinite(config__15_inline_8 -> bias) || !__builtin_isfinite(config__15_inline_8 -> white_noise_std) || !__builtin_isfinite(config__15_inline_8 -> random_walk_std) || !__builtin_isfinite(config__15_inline_8 -> min_value) || !__builtin_isfinite(config__15_inline_8 -> max_value) || !__builtin_isfinite(config__15_inline_8 -> resolution) || !__builtin_isfinite(config__15_inline_8 -> dropout_probability) || config__15_inline_8 -> white_noise_std < 0.0 || config__15_inline_8 -> random_walk_std < 0.0 || config__15_inline_8 -> min_value > config__15_inline_8 -> max_value || config__15_inline_8 -> resolution < 0.0 || config__15_inline_8 -> dropout_probability < 0.0 || config__15_inline_8 -> dropout_probability > 1.0) {{
            status = SIM_ERR_OUT_OF_RANGE;
            goto rose_inline_end__16;
          }
        }
{
          status = SIM_OK;
          goto rose_inline_end__16;
        }
        rose_inline_end__16:
        ;
      }
    }
    if (status != SIM_OK) {
      return status;
    }
  }
  return SIM_OK;
}
/** @brief 初始化导引头随机状态和固定容量延迟缓冲区。 */

SimStatus sensor_seeker_init(SeekerSensor *sensor,const SeekerSensorConfig *config,uint64_t random_seed,double simulation_dt_s)
{
  SimStatus status;
  if (sensor == 0 || config == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  status = sensor_seeker_validate(config,simulation_dt_s);
  if (status != SIM_OK) {
    return status;
  }
  memset(sensor,0,sizeof(( *sensor)));
  sensor -> config =  *config;
  status = seeker_delay_steps(config -> delay_s,simulation_dt_s,&sensor -> delay_steps);
  if (status != SIM_OK) {
    return status;
  }
  status = ring_buffer_init(&sensor -> delay_buffer,(sensor -> delay_storage),(1024u + 1u),sizeof(sensor -> delay_storage[0]));
  if (status != SIM_OK) {
    return status;
  }
  sim_random_seed(&sensor -> random,random_seed);
  return SIM_OK;
}
/** @brief 计算无误差弹目距离、LOS、LOS 角速度和闭合速度。 */

C2CUDA_HOST_DEVICE static SimStatus evaluate_truth(const SeekerTruth *truth,SeekerMeasurement *measurement)
{
  Vec3 relative_position;
  Vec3 relative_velocity;
  double range;
  SimStatus status;
  if (truth == 0 || measurement == 0 || !vec3_isfinite(truth -> missile_position_ecef_m) || !vec3_isfinite(truth -> missile_velocity_ecef_mps) || !vec3_isfinite(truth -> target_position_ecef_m) || !vec3_isfinite(truth -> target_velocity_ecef_mps)) {
    return SIM_ERR_INVALID_ARG;
  }
  relative_position = vec3_sub(truth -> target_position_ecef_m,truth -> missile_position_ecef_m);
  relative_velocity = vec3_sub(truth -> target_velocity_ecef_mps,truth -> missile_velocity_ecef_mps);
  range = vec3_norm(relative_position);
  if (!__builtin_isfinite(range) || range <= 1.0e-9) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  status = vec3_normalize(relative_position,&measurement -> los_unit_ecef);
  if (status != SIM_OK) {
    return status;
  }
  measurement -> range_m = range;
  measurement -> los_rate_ecef_radps = vec3_scale((vec3_cross(relative_position,relative_velocity)),1.0 / (range * range));
  measurement -> closing_velocity_mps = -vec3_dot(measurement -> los_unit_ecef,relative_velocity);
  return SIM_OK;
}
/** @brief 对完整导引头真值应用各通道误差和整帧丢包。 */

C2CUDA_HOST_DEVICE static SimStatus sample_truth(SeekerSensor *sensor,double sim_time_s,double simulation_dt_s,const SeekerTruth *truth)
{
  SeekerMeasurement truth_measurement;
  SeekerDelayedSample sample;
  double los_unit_values[3];
  double los_rate_values[3];
  double noise_dt_s;
  size_t index;
  SimStatus status;
  if (sensor -> have_sample != 0 && sim_time_s + 1.0e-12 < sensor -> next_sample_time_s) {
    return SIM_OK;
  }
  status = evaluate_truth(truth,&truth_measurement);
  if (status != SIM_OK) {
    return status;
  }
  memset((&sample),0,sizeof(sample));
  sample . valid = 1;
  noise_dt_s = (sensor -> have_sample != 0?sensor -> config . sample_period_s : simulation_dt_s);
  status = apply_scalar_noise((&sensor -> config . range),&sensor -> range_state,&sensor -> random,truth_measurement . range_m,noise_dt_s,&sample . measurement . range_m);
  if (status != SIM_OK) {
    return status;
  }
  status = apply_scalar_noise((&sensor -> config . closing_velocity),&sensor -> closing_velocity_state,&sensor -> random,truth_measurement . closing_velocity_mps,noise_dt_s,&sample . measurement . closing_velocity_mps);
  if (status != SIM_OK) {
    return status;
  }
  for (index = 0u; index < 3u; ++index) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;{
      const SensorNoiseConfig *config__18_inline_9 = (&sensor -> config . los_unit_axis[index]);
      SensorNoiseState *state__19_inline_10 = &sensor -> los_unit_state[index];
      SimRandom *random__20_inline_11 = &sensor -> random;
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
      double rose_temp__29;
{
        Vec3 value__26 = truth_measurement . los_unit_ecef;
        size_t index__27 = index;
        if (index__27 == 0u) {{
            rose_temp__29 = value__26 . x;
            goto rose_inline_end__28;
          }
        }
{
          rose_temp__29 = (index__27 == 1u?value__26 . y : value__26 . z);
          goto rose_inline_end__28;
        }
        rose_inline_end__28:
        ;
      }
      double truth__21_inline_12 = rose_temp__29;
      double dt_s__22_inline_13 = noise_dt_s;
      double *measurement__23_inline_14 = &los_unit_values[index];
      SensorNoiseConfig component_config_inline_15;
      int dropped_inline_16 = 0;
      if (config__18_inline_9 == 0) {{
          status = SIM_ERR_INVALID_ARG;
          goto rose_inline_end__24;
        }
      }
      component_config_inline_15 =  *config__18_inline_9;
      component_config_inline_15 . dropout_probability = 0.0;
{
        status = sensor_noise_apply((&component_config_inline_15),state__19_inline_10,random__20_inline_11,truth__21_inline_12,dt_s__22_inline_13,measurement__23_inline_14,&dropped_inline_16);
        goto rose_inline_end__24;
      }
      rose_inline_end__24:
      ;
    }
    if (status == SIM_OK) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;{
        const SensorNoiseConfig *config__31_inline_17 = (&sensor -> config . los_rate_axis[index]);
        SensorNoiseState *state__32_inline_18 = &sensor -> los_rate_state[index];
        SimRandom *random__33_inline_19 = &sensor -> random;
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
        double rose_temp__42;
{
          Vec3 value__39 = truth_measurement . los_rate_ecef_radps;
          size_t index__40 = index;
          if (index__40 == 0u) {{
              rose_temp__42 = value__39 . x;
              goto rose_inline_end__41;
            }
          }
{
            rose_temp__42 = (index__40 == 1u?value__39 . y : value__39 . z);
            goto rose_inline_end__41;
          }
          rose_inline_end__41:
          ;
        }
        double truth__34_inline_20 = rose_temp__42;
        double dt_s__35_inline_21 = noise_dt_s;
        double *measurement__36_inline_22 = &los_rate_values[index];
        SensorNoiseConfig component_config_inline_23;
        int dropped_inline_24 = 0;
        if (config__31_inline_17 == 0) {{
            status = SIM_ERR_INVALID_ARG;
            goto rose_inline_end__37;
          }
        }
        component_config_inline_23 =  *config__31_inline_17;
        component_config_inline_23 . dropout_probability = 0.0;
{
          status = sensor_noise_apply((&component_config_inline_23),state__32_inline_18,random__33_inline_19,truth__34_inline_20,dt_s__35_inline_21,measurement__36_inline_22,&dropped_inline_24);
          goto rose_inline_end__37;
        }
        rose_inline_end__37:
        ;
      }
    }
    if (status != SIM_OK) {
      return status;
    }
  }
  status = vec3_normalize((vec3_make(los_unit_values[0],los_unit_values[1],los_unit_values[2])),&sample . measurement . los_unit_ecef);
  sample . valid = (((unsigned int )status) != ((unsigned int )SIM_OK)?0 : sample . valid);
  sample . measurement . los_rate_ecef_radps = vec3_make(los_rate_values[0],los_rate_values[1],los_rate_values[2]);
  sample . dropped = sim_random_uniform01(&sensor -> random) < sensor -> config . dropout_probability;
  sample . valid = (sample . dropped != 0?0 : sample . valid);
  sensor -> held_sample = sample;
  sensor -> have_sample = 1;
  sensor -> next_sample_time_s = sim_time_s + sensor -> config . sample_period_s;
  return SIM_OK;
}
/** @brief 输出延迟后的完整导引头测量及状态。 */

SimStatus sensor_seeker_update(SeekerSensor *sensor,double sim_time_s,double simulation_dt_s,const SeekerTruth *truth,SeekerMeasurement *measurement,SensorSampleStatus *sample_status)
{
  SeekerDelayedSample delayed_sample;
  SimStatus status;
  if (sensor == 0 || truth == 0 || measurement == 0 || sample_status == 0 || !__builtin_isfinite(sim_time_s) || sim_time_s < 0.0 || !__builtin_isfinite(simulation_dt_s) || simulation_dt_s <= 0.0) {
    return SIM_ERR_INVALID_ARG;
  }
  memset(measurement,0,sizeof(( *measurement)));
  memset(sample_status,0,sizeof(( *sample_status)));
  if (sensor -> config . enabled == 0) {
    return SIM_OK;
  }
  status = sample_truth(sensor,sim_time_s,simulation_dt_s,truth);
  if (status != SIM_OK) {
    return status;
  }
  status = ring_buffer_push(&sensor -> delay_buffer,(&sensor -> held_sample));
  if (status != SIM_OK) {
    return status;
  }
  if (ring_buffer_count((&sensor -> delay_buffer)) <= sensor -> delay_steps) {
    sample_status -> delay_warmup = 1;
    return SIM_OK;
  }
  status = ring_buffer_get_delayed((&sensor -> delay_buffer),sensor -> delay_steps,(&delayed_sample));
  if (status != SIM_OK) {
    return status;
  }
   *measurement = delayed_sample . measurement;
  sample_status -> valid = delayed_sample . valid;
  sample_status -> dropped = delayed_sample . dropped;
  return SIM_OK;
}
