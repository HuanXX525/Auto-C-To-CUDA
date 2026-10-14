/** @file sensor_accel.c
 *  @brief ECEF 三轴加速度计测量模型实现。
 */
#include "env/sensor_accel.h"

SimStatus sensor_accel_init(
    AccelSensor *sensor,
    const AccelSensorConfig *config,
    uint64_t random_seed,
    double simulation_dt_s)
{
    return sensor_vector3_init(sensor, config, random_seed, simulation_dt_s);
}

SimStatus sensor_accel_update(
    AccelSensor *sensor,
    double sim_time_s,
    double simulation_dt_s,
    Vec3 acceleration_ecef_truth_mps2,
    Vec3 *acceleration_measurement_ecef_mps2,
    SensorSampleStatus *sample_status)
{
    return sensor_vector3_update(
        sensor,
        sim_time_s,
        simulation_dt_s,
        acceleration_ecef_truth_mps2,
        acceleration_measurement_ecef_mps2,
        sample_status);
}
