/** @file sensor_imu.h
 *  @brief IMU 三轴陀螺仪传感器模型。
 */
#ifndef ENV_SENSOR_IMU_H
#define ENV_SENSOR_IMU_H

#include "env/sensor_vector3.h"
#include "common/device_qualifiers.h"

/** @brief IMU 陀螺仪配置。 */
typedef SensorVector3Config ImuSensorConfig;
/** @brief IMU 陀螺仪实例私有状态。 */
typedef SensorVector3 ImuSensor;

/** @brief 初始化 IMU 陀螺仪模型。 */
C2CUDA_HOST_DEVICE SimStatus sensor_imu_init(
    ImuSensor *sensor,
    const ImuSensorConfig *config,
    uint64_t random_seed,
    double simulation_dt_s);

/** @brief 由机体系真实角速度生成陀螺仪测量。 */
C2CUDA_HOST_DEVICE SimStatus sensor_imu_update(
    ImuSensor *sensor,
    double sim_time_s,
    double simulation_dt_s,
    Vec3 omega_body_truth_radps,
    Vec3 *gyro_measurement_radps,
    SensorSampleStatus *sample_status);

#endif
