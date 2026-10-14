/** @file sensor_accel.h
 *  @brief ECEF 三轴加速度计测量模型。
 */
#ifndef ENV_SENSOR_ACCEL_H
#define ENV_SENSOR_ACCEL_H

#include "env/sensor_vector3.h"
#include "common/device_qualifiers.h"

/** @brief 加速度计配置。 */
typedef SensorVector3Config AccelSensorConfig;
/** @brief 加速度计实例私有状态。 */
typedef SensorVector3 AccelSensor;

/** @brief 初始化加速度计模型。 */
C2CUDA_HOST_DEVICE SimStatus sensor_accel_init(
    AccelSensor *sensor,
    const AccelSensorConfig *config,
    uint64_t random_seed,
    double simulation_dt_s);

/** @brief 由 ECEF 真实运动学加速度生成测量。 */
C2CUDA_HOST_DEVICE SimStatus sensor_accel_update(
    AccelSensor *sensor,
    double sim_time_s,
    double simulation_dt_s,
    Vec3 acceleration_ecef_truth_mps2,
    Vec3 *acceleration_measurement_ecef_mps2,
    SensorSampleStatus *sample_status);

#endif
