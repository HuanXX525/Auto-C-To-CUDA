/** @file sensor_imu.c
 *  @brief IMU 三轴陀螺仪传感器模型实现。
 */
#include "env/sensor_imu.h"

SimStatus sensor_imu_init(
    ImuSensor *sensor,
    const ImuSensorConfig *config,
    uint64_t random_seed,
    double simulation_dt_s)
{
    return sensor_vector3_init(sensor, config, random_seed, simulation_dt_s);
}

SimStatus sensor_imu_update(
    ImuSensor *sensor,
    double sim_time_s,
    double simulation_dt_s,
    Vec3 omega_body_truth_radps,
    Vec3 *gyro_measurement_radps,
    SensorSampleStatus *sample_status)
{
    return sensor_vector3_update(
        sensor,
        sim_time_s,
        simulation_dt_s,
        omega_body_truth_radps,
        gyro_measurement_radps,
        sample_status);
}
