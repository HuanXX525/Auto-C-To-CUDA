/** @file sensor_vector3.c
 *  @brief 三轴传感器共享采样、误差和延迟引擎实现。
 */
#include "env/sensor_vector3.h"
#include <math.h>
#include <string.h>
/** @brief 返回向量指定分量。 */
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

static double vector_component(Vec3 value,size_t index)
{
  if (index == 0u) {
    return value . x;
  }
  return index == 1u?value . y : value . z;
}
/** @brief 由三个标量构造向量。 */

C2CUDA_HOST_DEVICE static Vec3 vector_from_components(const double values[3])
{
  return vec3_make(values[0],values[1],values[2]);
}
/** @brief 将秒延迟向上取整为仿真步，保证实际延迟不短于配置值。 */

C2CUDA_HOST_DEVICE static SimStatus delay_seconds_to_steps(double delay_s,double simulation_dt_s,size_t *delay_steps)
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
/** @brief 校验采样、延迟、概率和三轴误差参数。 */

SimStatus sensor_vector3_validate(const SensorVector3Config *config,double simulation_dt_s)
{
  size_t delay_steps;
  size_t index;
  SimStatus status;
  if (config == 0 || !__builtin_isfinite(config -> sample_period_s) || config -> sample_period_s <= 0.0 || !__builtin_isfinite(config -> dropout_probability) || config -> dropout_probability < 0.0 || config -> dropout_probability > 1.0) {
    return SIM_ERR_INVALID_ARG;
  }
  status = delay_seconds_to_steps(config -> delay_s,simulation_dt_s,&delay_steps);
  if (status != SIM_OK) {
    return status;
  }
  for (index = 0u; index < 3u; ++index) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;{
      const SensorNoiseConfig *config__49_inline_25 = &config -> axis[index];
      if (config__49_inline_25 == 0) {{
          status = SIM_ERR_INVALID_ARG;
          goto rose_inline_end__50;
        }
      }
      if (!__builtin_isfinite(config__49_inline_25 -> bias) || !__builtin_isfinite(config__49_inline_25 -> white_noise_std) || !__builtin_isfinite(config__49_inline_25 -> random_walk_std) || !__builtin_isfinite(config__49_inline_25 -> min_value) || !__builtin_isfinite(config__49_inline_25 -> max_value) || !__builtin_isfinite(config__49_inline_25 -> resolution) || !__builtin_isfinite(config__49_inline_25 -> dropout_probability) || config__49_inline_25 -> white_noise_std < 0.0 || config__49_inline_25 -> random_walk_std < 0.0 || config__49_inline_25 -> min_value > config__49_inline_25 -> max_value || config__49_inline_25 -> resolution < 0.0 || config__49_inline_25 -> dropout_probability < 0.0 || config__49_inline_25 -> dropout_probability > 1.0) {{
          status = SIM_ERR_OUT_OF_RANGE;
          goto rose_inline_end__50;
        }
      }
{
        status = SIM_OK;
        goto rose_inline_end__50;
      }
      rose_inline_end__50:
      ;
    }
    if (status != SIM_OK) {
      return status;
    }
  }
  return SIM_OK;
}
/** @brief 初始化实例私有随机状态、误差状态和延迟缓冲区。 */

SimStatus sensor_vector3_init(SensorVector3 *sensor,const SensorVector3Config *config,uint64_t random_seed,double simulation_dt_s)
{
  SimStatus status;
  if (sensor == 0 || config == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  status = sensor_vector3_validate(config,simulation_dt_s);
  if (status != SIM_OK) {
    return status;
  }
  memset(sensor,0,sizeof(( *sensor)));
  sensor -> config =  *config;
  status = delay_seconds_to_steps(config -> delay_s,simulation_dt_s,&sensor -> delay_steps);
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
/** @brief 按采样周期生成新样本，并在非采样步保持最近样本。 */

C2CUDA_HOST_DEVICE static SimStatus sample_truth(SensorVector3 *sensor,double sim_time_s,double simulation_dt_s,Vec3 truth)
{
  SensorVector3Sample sample;
  double measured[3];
  double noise_dt_s;
  size_t index;
  SimStatus status;
  if (sensor -> have_sample != 0 && sim_time_s + 1.0e-12 < sensor -> next_sample_time_s) {
    return SIM_OK;
  }
  memset((&sample),0,sizeof(sample));
  sample . valid = 1;
  noise_dt_s = (sensor -> have_sample != 0?sensor -> config . sample_period_s : simulation_dt_s);
  for (index = 0u; index < 3u; ++index) {
    SensorNoiseConfig component_config = sensor -> config . axis[index];
    int component_dropped = 0;
    component_config . dropout_probability = 0.0;
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
    double rose_temp__47;
{
      Vec3 value__44 = truth;
      size_t index__45 = index;
      if (index__45 == 0u) {{
          rose_temp__47 = value__44 . x;
          goto rose_inline_end__46;
        }
      }
{
        rose_temp__47 = (index__45 == 1u?value__44 . y : value__44 . z);
        goto rose_inline_end__46;
      }
      rose_inline_end__46:
      ;
    }
    status = sensor_noise_apply((&component_config),&sensor -> noise_state[index],&sensor -> random,rose_temp__47,noise_dt_s,&measured[index],&component_dropped);
    if (status != SIM_OK) {
      return status;
    }
  }
  sample . measurement = vector_from_components(measured);
  sample . dropped = sim_random_uniform01(&sensor -> random) < sensor -> config . dropout_probability;
  sample . valid = (sample . dropped != 0?0 : sample . valid);
  sensor -> held_sample = sample;
  sensor -> have_sample = 1;
  sensor -> next_sample_time_s = sim_time_s + sensor -> config . sample_period_s;
  return SIM_OK;
}
/** @brief 输出固定延迟后的测量样本和有效状态。 */

SimStatus sensor_vector3_update(SensorVector3 *sensor,double sim_time_s,double simulation_dt_s,Vec3 truth,Vec3 *measurement,SensorSampleStatus *sample_status)
{
  SensorVector3Sample delayed_sample;
  SimStatus status;
  if (sensor == 0 || measurement == 0 || sample_status == 0 || !__builtin_isfinite(sim_time_s) || sim_time_s < 0.0 || !__builtin_isfinite(simulation_dt_s) || simulation_dt_s <= 0.0 || !vec3_isfinite(truth)) {
    return SIM_ERR_INVALID_ARG;
  }
  memset(sample_status,0,sizeof(( *sample_status)));
   *measurement = vec3_make(0.0,0.0,0.0);
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
