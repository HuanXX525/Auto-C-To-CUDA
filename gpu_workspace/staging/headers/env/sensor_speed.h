/** @file sensor_speed.h
 *  @brief ECEF 三轴速度计测量模型。
 */
#ifndef ENV_SENSOR_SPEED_H
#define ENV_SENSOR_SPEED_H

#include "env/sensor_vector3.h"
#include "common/device_qualifiers.h"

/** @brief 速度计配置。 */
typedef SensorVector3Config SpeedSensorConfig;
/** @brief 速度计实例私有状态。 */
typedef SensorVector3 SpeedSensor;

/** @brief 初始化速度计模型。 */
C2CUDA_HOST_DEVICE SimStatus sensor_speed_init(
    SpeedSensor *sensor,
    const SpeedSensorConfig *config,
    uint64_t random_seed,
    double simulation_dt_s);

/** @brief 由 ECEF 真实速度生成测量。 */
C2CUDA_HOST_DEVICE SimStatus sensor_speed_update(
    SpeedSensor *sensor,
    double sim_time_s,
    double simulation_dt_s,
    Vec3 velocity_ecef_truth_mps,
    Vec3 *velocity_measurement_ecef_mps,
    SensorSampleStatus *sample_status);

#endif
