/** @file missile_plant_6dof.c
 *  @brief 六自由度刚体方程和 Euler/RK2/RK4 积分实现。
 *
 *  姿态四元数按机体系到 ECEF 的主动旋转解释。单步期间外力、外力矩、
 *  重力和地球自转角速度保持常值，但力方向会随 RK 子步姿态重新变换。
 */
#include "env/missile_plant_6dof.h"

#include <math.h>

/** @brief 积分器内部使用的最小连续状态。 */
typedef struct PlantContinuousState {
    Vec3 position;
    Vec3 velocity;
    Quat attitude;
    Vec3 omega_body;
} PlantContinuousState;

/** @brief 连续状态对时间的导数。 */
typedef struct PlantDerivative {
    Vec3 position_rate;
    Vec3 velocity_rate;
    Quat attitude_rate;
    Vec3 omega_rate;
} PlantDerivative;

/** @brief 对四元数执行逐分量加法。 */
static Quat quat_add(Quat left, Quat right)
{
    Quat result = {
        left.w + right.w,
        left.x + right.x,
        left.y + right.y,
        left.z + right.z
    };
    return result;
}

/** @brief 对四元数执行逐分量数乘。 */
static Quat quat_scale(Quat value, double scale)
{
    Quat result = {
        value.w * scale,
        value.x * scale,
        value.y * scale,
        value.z * scale
    };
    return result;
}

/** @brief 将状态沿导数方向推进指定子步。 */
static PlantContinuousState state_add_scaled(
    PlantContinuousState state,
    PlantDerivative derivative,
    double scale)
{
    PlantContinuousState result;

    result.position = vec3_add(state.position, vec3_scale(derivative.position_rate, scale));
    result.velocity = vec3_add(state.velocity, vec3_scale(derivative.velocity_rate, scale));
    result.attitude = quat_add(state.attitude, quat_scale(derivative.attitude_rate, scale));
    result.omega_body = vec3_add(state.omega_body, vec3_scale(derivative.omega_rate, scale));
    return result;
}

/** @brief 计算给定连续状态下的六自由度导数。 */
static SimStatus evaluate_derivative(
    const PlantContinuousState *state,
    double mass,
    Matrix3 inertia,
    const PlantInput6Dof *input,
    PlantDerivative *out)
{
    Matrix3 body_to_ecef;
    Matrix3 inertia_inverse;
    Vec3 force_ecef;
    Vec3 angular_momentum;
    Vec3 rotation_acceleration = vec3_make(0.0, 0.0, 0.0);
    Quat omega_quat = {
        0.0,
        state->omega_body.x,
        state->omega_body.y,
        state->omega_body.z
    };
    SimStatus status;

    status = quat_to_dcm(state->attitude, &body_to_ecef);
    if (status != SIM_OK) {
        return status;
    }
    status = matrix3_inverse(inertia, &inertia_inverse);
    if (status != SIM_OK) {
        return status;
    }
    force_ecef = matrix3_multiply_vec3(body_to_ecef, input->force_b_n);
    if (input->enable_earth_rotation != 0) {
        const Vec3 coriolis = vec3_scale(
            vec3_cross(input->earth_rotation_ecef_radps, state->velocity),
            -2.0);
        const Vec3 centrifugal = vec3_scale(
            vec3_cross(
                input->earth_rotation_ecef_radps,
                vec3_cross(input->earth_rotation_ecef_radps, state->position)),
            -1.0);
        rotation_acceleration = vec3_add(coriolis, centrifugal);
    }

    angular_momentum = matrix3_multiply_vec3(inertia, state->omega_body);
    out->position_rate = state->velocity;
    out->velocity_rate = vec3_add(
        vec3_add(vec3_scale(force_ecef, 1.0 / mass), input->gravity_ecef_mps2),
        rotation_acceleration);
    out->attitude_rate = quat_scale(
        quat_multiply(state->attitude, omega_quat),
        0.5);
    out->omega_rate = matrix3_multiply_vec3(
        inertia_inverse,
        vec3_sub(
            input->moment_b_nm,
            vec3_cross(state->omega_body, angular_momentum)));
    return SIM_OK;
}

/** @brief 对四组 RK 导数执行加权求和。 */
static PlantDerivative rk4_weighted(
    PlantDerivative k1,
    PlantDerivative k2,
    PlantDerivative k3,
    PlantDerivative k4)
{
    PlantDerivative result;

    result.position_rate = vec3_scale(
        vec3_add(
            vec3_add(k1.position_rate, vec3_scale(k2.position_rate, 2.0)),
            vec3_add(vec3_scale(k3.position_rate, 2.0), k4.position_rate)),
        1.0 / 6.0);
    result.velocity_rate = vec3_scale(
        vec3_add(
            vec3_add(k1.velocity_rate, vec3_scale(k2.velocity_rate, 2.0)),
            vec3_add(vec3_scale(k3.velocity_rate, 2.0), k4.velocity_rate)),
        1.0 / 6.0);
    result.attitude_rate = quat_scale(
        quat_add(
            quat_add(k1.attitude_rate, quat_scale(k2.attitude_rate, 2.0)),
            quat_add(quat_scale(k3.attitude_rate, 2.0), k4.attitude_rate)),
        1.0 / 6.0);
    result.omega_rate = vec3_scale(
        vec3_add(
            vec3_add(k1.omega_rate, vec3_scale(k2.omega_rate, 2.0)),
            vec3_add(vec3_scale(k3.omega_rate, 2.0), k4.omega_rate)),
        1.0 / 6.0);
    return result;
}

/** @brief 校验刚体状态及惯量矩阵可逆性。 */
SimStatus missile_plant_validate(const PlantState6Dof *state)
{
    Matrix3 inverse;

    if (state == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!isfinite(state->time) || !vec3_isfinite(state->pos_ecef) ||
        !vec3_isfinite(state->vel_ecef) || !vec3_isfinite(state->accel_ecef) ||
        !quat_isfinite(state->q_bi) || !vec3_isfinite(state->omega_b) ||
        !vec3_isfinite(state->alpha_b) || !isfinite(state->mass) ||
        state->mass <= 0.0 || !matrix3_isfinite(state->inertia_b)) {
        return SIM_ERR_NUMERIC;
    }
    return matrix3_inverse(state->inertia_b, &inverse);
}

/** @brief 使用 Euler、RK2 或 RK4 推进一次刚体状态。 */
SimStatus missile_plant_step(
    PlantState6Dof *state,
    const PlantInput6Dof *input,
    double dt,
    IntegratorType integrator)
{
    PlantContinuousState initial;
    PlantContinuousState next;
    PlantDerivative k1;
    PlantDerivative k2;
    PlantDerivative k3;
    PlantDerivative k4;
    PlantDerivative combined;
    Quat normalized;
    SimStatus status;

    if (state == 0 || input == 0 || !isfinite(dt) || dt <= 0.0 ||
        !vec3_isfinite(input->force_b_n) ||
        !vec3_isfinite(input->moment_b_nm) ||
        !vec3_isfinite(input->gravity_ecef_mps2) ||
        !vec3_isfinite(input->earth_rotation_ecef_radps)) {
        return SIM_ERR_INVALID_ARG;
    }
    status = missile_plant_validate(state);
    if (status != SIM_OK) {
        return status;
    }

    initial.position = state->pos_ecef;
    initial.velocity = state->vel_ecef;
    initial.attitude = state->q_bi;
    initial.omega_body = state->omega_b;
    status = evaluate_derivative(
        &initial,
        state->mass,
        state->inertia_b,
        input,
        &k1);
    if (status != SIM_OK) {
        return status;
    }

    if (integrator == INTEGRATOR_EULER) {
        combined = k1;
    } else if (integrator == INTEGRATOR_RK2) {
        PlantContinuousState midpoint = state_add_scaled(initial, k1, 0.5 * dt);
        status = evaluate_derivative(
            &midpoint,
            state->mass,
            state->inertia_b,
            input,
            &combined);
        if (status != SIM_OK) {
            return status;
        }
    } else if (integrator == INTEGRATOR_RK4) {
        PlantContinuousState sample;

        sample = state_add_scaled(initial, k1, 0.5 * dt);
        status = evaluate_derivative(
            &sample,
            state->mass,
            state->inertia_b,
            input,
            &k2);
        if (status == SIM_OK) {
            sample = state_add_scaled(initial, k2, 0.5 * dt);
            status = evaluate_derivative(
                &sample,
                state->mass,
                state->inertia_b,
                input,
                &k3);
        }
        if (status == SIM_OK) {
            sample = state_add_scaled(initial, k3, dt);
            status = evaluate_derivative(
                &sample,
                state->mass,
                state->inertia_b,
                input,
                &k4);
        }
        if (status != SIM_OK) {
            return status;
        }
        combined = rk4_weighted(k1, k2, k3, k4);
    } else {
        return SIM_ERR_OUT_OF_RANGE;
    }

    next = state_add_scaled(initial, combined, dt);
    status = quat_normalize(next.attitude, &normalized);
    if (status != SIM_OK) {
        return status;
    }
    state->time += dt;
    state->pos_ecef = next.position;
    state->vel_ecef = next.velocity;
    state->q_bi = normalized;
    state->omega_b = next.omega_body;
    state->accel_ecef = combined.velocity_rate;
    state->alpha_b = combined.omega_rate;
    state->force_b = input->force_b_n;
    state->moment_b = input->moment_b_nm;
    return missile_plant_validate(state);
}
