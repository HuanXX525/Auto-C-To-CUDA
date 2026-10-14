/** @file sensor_noise.c
 *  @brief 标量传感器误差、量化、限幅和丢包模型实现。
 */
#include "env/sensor_noise.h"
#include <math.h>
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

C2CUDA_HOST_DEVICE static double clamp_value(double value,double minimum,double maximum)
{
  return value < minimum?minimum : ((value > maximum?maximum : value));
}
/** @brief 检查传感器误差配置是否满足概率和数值范围约束。 */

SimStatus sensor_noise_validate(const SensorNoiseConfig *config)
{
  if (config == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (!__builtin_isfinite(config -> bias) || !__builtin_isfinite(config -> white_noise_std) || !__builtin_isfinite(config -> random_walk_std) || !__builtin_isfinite(config -> min_value) || !__builtin_isfinite(config -> max_value) || !__builtin_isfinite(config -> resolution) || !__builtin_isfinite(config -> dropout_probability) || config -> white_noise_std < 0.0 || config -> random_walk_std < 0.0 || config -> min_value > config -> max_value || config -> resolution < 0.0 || config -> dropout_probability < 0.0 || config -> dropout_probability > 1.0) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  return SIM_OK;
}
/** @brief 按统一传感器误差方程生成一个标量测量样本。 */

SimStatus sensor_noise_apply(const SensorNoiseConfig *config,SensorNoiseState *state,SimRandom *random,double truth,double dt,double *measurement,int *dropped)
{
  double value;
  SimStatus status;
  if (state == 0 || random == 0 || measurement == 0 || dropped == 0 || !__builtin_isfinite(truth) || !__builtin_isfinite(dt) || dt < 0.0) {
    return SIM_ERR_INVALID_ARG;
  }
  status = sensor_noise_validate(config);
  if (status != SIM_OK) {
    return status;
  }
   *dropped = sim_random_uniform01(random) < config -> dropout_probability;
  if ( *dropped != 0) {
    return SIM_OK;
  }
  state -> random_walk_value += sim_random_normal(random,0.0,config -> random_walk_std * sqrt(dt));
  value = truth + config -> bias + state -> random_walk_value + sim_random_normal(random,0.0,config -> white_noise_std);
  if (config -> resolution > 0.0) {
    value = round(value / config -> resolution) * config -> resolution;
  }
   *measurement = clamp_value(value,config -> min_value,config -> max_value);
  return SIM_OK;
}
