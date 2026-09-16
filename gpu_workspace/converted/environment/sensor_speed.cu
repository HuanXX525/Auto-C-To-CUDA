/** @file sensor_speed.c
 *  @brief ECEF 三轴速度计测量模型实现。
 */
#include "env/sensor_speed.h"
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

SimStatus sensor_speed_init(SpeedSensor *sensor,const SpeedSensorConfig *config,uint64_t random_seed,double simulation_dt_s)
{
  return sensor_vector3_init(sensor,config,random_seed,simulation_dt_s);
}

SimStatus sensor_speed_update(SpeedSensor *sensor,double sim_time_s,double simulation_dt_s,Vec3 velocity_ecef_truth_mps,Vec3 *velocity_measurement_ecef_mps,SensorSampleStatus *sample_status)
{
  return sensor_vector3_update(sensor,sim_time_s,simulation_dt_s,velocity_ecef_truth_mps,velocity_measurement_ecef_mps,sample_status);
}
