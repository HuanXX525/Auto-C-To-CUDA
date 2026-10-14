/** @file navigation.c
 *  @brief 传感器帧到飞控导航状态的转换实现。
 */
#include "fc/navigation.h"

#include <math.h>
#include <string.h>

SimStatus navigation_update_from_sensor(const SensorFrame *sensor, NavState *out)
{
    if (sensor == 0 || out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!isfinite(sensor->sim_time) ||
        !isfinite(sensor->target_range_meas) ||
        !isfinite(sensor->target_closing_velocity_meas) ||
        !vec3_isfinite(sensor->missile_vel_ecef_meas) ||
        !vec3_isfinite(sensor->missile_accel_ecef_meas) ||
        !vec3_isfinite(sensor->missile_gyro_b_meas) ||
        !vec3_isfinite(sensor->target_los_unit_ecef_meas) ||
        !vec3_isfinite(sensor->target_los_rate_ecef_meas)) {
        return SIM_ERR_NUMERIC;
    }

    (void)memset(out, 0, sizeof(*out));
    out->time = sensor->sim_time;
    if ((sensor->sensor_valid_flags & SIM_SENSOR_VALID_SPEED) != 0u ||
        (sensor->sensor_valid_flags & SIM_SENSOR_VALID_ACCEL) != 0u ||
        (sensor->sensor_valid_flags & SIM_SENSOR_VALID_IMU_GYRO) != 0u) {
        out->missile_vel_ecef_est = sensor->missile_vel_ecef_meas;
        out->missile_accel_ecef_est = sensor->missile_accel_ecef_meas;
        out->omega_b_est = sensor->missile_gyro_b_meas;
        out->valid_flags |= FC_NAV_VALID_KINEMATICS;
    }
    if ((sensor->sensor_valid_flags & SIM_SENSOR_VALID_GEODETIC) != 0u) {
        if (!isfinite(sensor->missile_lat_rad_meas) ||
            !isfinite(sensor->missile_lon_rad_meas) ||
            !isfinite(sensor->missile_height_m_meas) ||
            !isfinite(sensor->missile_height_agl_m_meas)) {
            return SIM_ERR_NUMERIC;
        }
        out->lat_rad = sensor->missile_lat_rad_meas;
        out->lon_rad = sensor->missile_lon_rad_meas;
        out->height_m = sensor->missile_height_m_meas;
        out->height_agl_m = sensor->missile_height_agl_m_meas;
        out->valid_flags |= FC_NAV_VALID_GEODETIC;
    }
    if ((sensor->sensor_valid_flags & SIM_SENSOR_VALID_SEEKER) != 0u) {
        out->range = sensor->target_range_meas;
        out->los_unit_ecef = sensor->target_los_unit_ecef_meas;
        out->los_rate_ecef = sensor->target_los_rate_ecef_meas;
        out->closing_velocity = sensor->target_closing_velocity_meas;
        out->valid_flags |= FC_NAV_VALID_SEEKER;
    }
    return SIM_OK;
}

int navigation_has_guidance_solution(const NavState *nav)
{
    return nav != 0 &&
        (nav->valid_flags & FC_NAV_VALID_SEEKER) != 0u &&
        isfinite(nav->range) &&
        isfinite(nav->closing_velocity) &&
        nav->range > 0.0 &&
        nav->closing_velocity > 0.0 &&
        vec3_isfinite(nav->los_unit_ecef) &&
        vec3_isfinite(nav->los_rate_ecef);
}
