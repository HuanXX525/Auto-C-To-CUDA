/** @file aero_model.c
 *  @brief 基础二次阻力和舵效模型实现。
 */
#include "env/aero_model.h"

#include <math.h>

/** @brief 计算沿相对气流反向的阻力及线性舵效力/力矩。 */
SimStatus aero_model_evaluate(
    const AeroModel *model,
    double density_kgpm3,
    double mach,
    Vec3 velocity_air_b_mps,
    double pitch_actuator_rad,
    double yaw_actuator_rad,
    Vec3 *force_b_n,
    Vec3 *moment_b_nm,
    uint32_t *model_flags)
{
    double speed;
    double dynamic_pressure;
    Vec3 drag_direction;

    if (model == 0 || force_b_n == 0 || moment_b_nm == 0 ||
        !isfinite(density_kgpm3) || density_kgpm3 < 0.0 ||
        !isfinite(mach) || mach < 0.0 ||
        !vec3_isfinite(velocity_air_b_mps) ||
        !isfinite(pitch_actuator_rad) || !isfinite(yaw_actuator_rad) ||
        !isfinite(model->reference_area_m2) || model->reference_area_m2 < 0.0 ||
        !isfinite(model->reference_length_m) || model->reference_length_m < 0.0 ||
        !isfinite(model->drag_coefficient) || model->drag_coefficient < 0.0 ||
        !isfinite(model->control_force_coefficient) ||
        !isfinite(model->control_moment_coefficient)) {
        return SIM_ERR_INVALID_ARG;
    }
    *force_b_n = vec3_make(0.0, 0.0, 0.0);
    *moment_b_nm = vec3_make(0.0, 0.0, 0.0);
    if (model_flags != 0) {
        *model_flags = 0u;
    }
    if (model->enabled == 0) {
        return SIM_OK;
    }
    speed = vec3_norm(velocity_air_b_mps);
    if (speed <= 1.0e-9) {
        return SIM_OK;
    }
    dynamic_pressure = 0.5 * density_kgpm3 * speed * speed;
    if (model->database != 0) {
        AeroTableSample coefficients;
        uint32_t lookup_flags = 0u;
        double alpha_rad = atan2(velocity_air_b_mps.z, velocity_air_b_mps.x);
        double beta_ratio = velocity_air_b_mps.y / speed;
        double beta_rad;
        SimStatus status;

        if (beta_ratio > 1.0) {
            beta_ratio = 1.0;
        } else if (beta_ratio < -1.0) {
            beta_ratio = -1.0;
        }
        beta_rad = asin(beta_ratio);
        status = aero_database_lookup(
            model->database,
            mach,
            alpha_rad,
            beta_rad,
            &coefficients,
            &lookup_flags);
        if (status != SIM_OK) {
            return status;
        }
        if (model_flags != 0) {
            *model_flags = lookup_flags;
        }
        *force_b_n = vec3_make(
            dynamic_pressure * model->reference_area_m2 * coefficients.cx,
            dynamic_pressure * model->reference_area_m2 *
                (coefficients.cy + (model->control_force_coefficient * yaw_actuator_rad)),
            dynamic_pressure * model->reference_area_m2 *
                (coefficients.cz + (model->control_force_coefficient * pitch_actuator_rad)));
        *moment_b_nm = vec3_make(
            dynamic_pressure * model->reference_area_m2 * model->reference_length_m *
                coefficients.cl,
            dynamic_pressure * model->reference_area_m2 * model->reference_length_m *
                (coefficients.cm + (model->control_moment_coefficient * pitch_actuator_rad)),
            dynamic_pressure * model->reference_area_m2 * model->reference_length_m *
                (coefficients.cn + (model->control_moment_coefficient * yaw_actuator_rad)));
        return SIM_OK;
    }
    if (model->surrogate != 0) {
        AeroSurrogateCoefficients coefficients;
        double alpha_rad = atan2(velocity_air_b_mps.z, velocity_air_b_mps.x);
        double beta_ratio = velocity_air_b_mps.y / speed;
        double beta_rad;
        SimStatus status;

        if (beta_ratio > 1.0) {
            beta_ratio = 1.0;
        } else if (beta_ratio < -1.0) {
            beta_ratio = -1.0;
        }
        beta_rad = asin(beta_ratio);
        status = aero_surrogate_evaluate(
            model->surrogate,
            mach,
            alpha_rad,
            beta_rad,
            &coefficients);
        if (status != SIM_OK) {
            return status;
        }
        *force_b_n = vec3_make(
            dynamic_pressure * model->reference_area_m2 * coefficients.cx,
            dynamic_pressure * model->reference_area_m2 *
                (coefficients.cy + (model->control_force_coefficient * yaw_actuator_rad)),
            dynamic_pressure * model->reference_area_m2 *
                (coefficients.cz + (model->control_force_coefficient * pitch_actuator_rad)));
        *moment_b_nm = vec3_make(
            dynamic_pressure * model->reference_area_m2 * model->reference_length_m *
                coefficients.cl,
            dynamic_pressure * model->reference_area_m2 * model->reference_length_m *
                (coefficients.cm + (model->control_moment_coefficient * pitch_actuator_rad)),
            dynamic_pressure * model->reference_area_m2 * model->reference_length_m *
                (coefficients.cn + (model->control_moment_coefficient * yaw_actuator_rad)));
        return SIM_OK;
    }
    drag_direction = vec3_scale(velocity_air_b_mps, -1.0 / speed);
    *force_b_n = vec3_add(
        vec3_scale(
            drag_direction,
            dynamic_pressure * model->reference_area_m2 * model->drag_coefficient),
        vec3_make(
            0.0,
            dynamic_pressure * model->reference_area_m2 *
                model->control_force_coefficient * yaw_actuator_rad,
            dynamic_pressure * model->reference_area_m2 *
                model->control_force_coefficient * pitch_actuator_rad));
    *moment_b_nm = vec3_make(
        0.0,
        dynamic_pressure * model->reference_area_m2 * model->reference_length_m *
            model->control_moment_coefficient * pitch_actuator_rad,
        dynamic_pressure * model->reference_area_m2 * model->reference_length_m *
            model->control_moment_coefficient * yaw_actuator_rad);
    return SIM_OK;
}
