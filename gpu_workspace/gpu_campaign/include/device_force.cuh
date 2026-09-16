#ifndef GPU_CAMPAIGN_DEVICE_FORCE_CUH
#define GPU_CAMPAIGN_DEVICE_FORCE_CUH

#include <cmath>
#include <cstdint>
#include <type_traits>

#include "common/device_qualifiers.h"
#include "common/matrix3.h"
#include "common/quaternion.h"
#include "common/status.h"
#include "common/vec3.h"
#include "device_environment.cuh"

namespace gpu_campaign {

/* Pointer-free subset of AeroModel used by the device table path. */
struct DeviceAeroForceConfig {
    std::uint32_t enabled;
    double reference_area_m2;
    double reference_length_m;
    double control_force_coefficient;
    double control_moment_coefficient;
};

C2CUDA_HOST_DEVICE inline SimStatus device_aero_force_evaluate(
    const DeviceAeroForceConfig *config, const DeviceAeroTable *table,
    double density_kgpm3, double mach, Vec3 velocity_air_b_mps,
    double pitch_actuator_rad, double yaw_actuator_rad, Vec3 *force_b_n,
    Vec3 *moment_b_nm, std::uint32_t *model_flags) {
    if (config == nullptr || force_b_n == nullptr || moment_b_nm == nullptr ||
        !__builtin_isfinite(density_kgpm3) || density_kgpm3 < 0.0 ||
        !__builtin_isfinite(mach) || mach < 0.0 ||
        !vec3_isfinite(velocity_air_b_mps) ||
        !__builtin_isfinite(pitch_actuator_rad) ||
        !__builtin_isfinite(yaw_actuator_rad) ||
        !__builtin_isfinite(config->reference_area_m2) ||
        config->reference_area_m2 < 0.0 ||
        !__builtin_isfinite(config->reference_length_m) ||
        config->reference_length_m < 0.0 ||
        !__builtin_isfinite(config->control_force_coefficient) ||
        !__builtin_isfinite(config->control_moment_coefficient)) {
        return SIM_ERR_INVALID_ARG;
    }
    *force_b_n = vec3_make(0.0, 0.0, 0.0);
    *moment_b_nm = vec3_make(0.0, 0.0, 0.0);
    if (model_flags != nullptr) *model_flags = 0u;
    if (config->enabled == 0u) return SIM_OK;
    const double speed = vec3_norm(velocity_air_b_mps);
    if (speed <= 1.0e-9) return SIM_OK;
    if (table == nullptr) return SIM_ERR_INVALID_ARG;
    const double dynamic_pressure = 0.5 * density_kgpm3 * speed * speed;
    const double alpha_rad = std::atan2(velocity_air_b_mps.z, velocity_air_b_mps.x);
    double beta_ratio = velocity_air_b_mps.y / speed;
    if (beta_ratio > 1.0) beta_ratio = 1.0;
    if (beta_ratio < -1.0) beta_ratio = -1.0;
    const double beta_rad = std::asin(beta_ratio);
    AeroTableSample coefficients{};
    std::uint32_t lookup_flags = 0u;
    const SimStatus status = device_aero_table_lookup(
        table, mach, alpha_rad, beta_rad, &coefficients, &lookup_flags);
    if (status != SIM_OK) return status;
    if (model_flags != nullptr) *model_flags = lookup_flags;
    *force_b_n = vec3_make(
        dynamic_pressure * config->reference_area_m2 * coefficients.cx,
        dynamic_pressure * config->reference_area_m2 *
            (coefficients.cy + config->control_force_coefficient * yaw_actuator_rad),
        dynamic_pressure * config->reference_area_m2 *
            (coefficients.cz + config->control_force_coefficient * pitch_actuator_rad));
    *moment_b_nm = vec3_make(
        dynamic_pressure * config->reference_area_m2 * config->reference_length_m * coefficients.cl,
        dynamic_pressure * config->reference_area_m2 * config->reference_length_m *
            (coefficients.cm + config->control_moment_coefficient * pitch_actuator_rad),
        dynamic_pressure * config->reference_area_m2 * config->reference_length_m *
            (coefficients.cn + config->control_moment_coefficient * yaw_actuator_rad));
    return SIM_OK;
}

static_assert(std::is_trivially_copyable<DeviceAeroForceConfig>::value,
              "DeviceAeroForceConfig must be trivially copyable");
static_assert(std::is_standard_layout<DeviceAeroForceConfig>::value,
              "DeviceAeroForceConfig must use standard layout");

/* Body-frame air velocity, mirroring environment_force_model.c:
   q_bi -> body-to-ECEF DCM, transpose to ECEF-to-body, then rotate
   (vel_ecef - wind_ecef). */
C2CUDA_HOST_DEVICE inline SimStatus device_air_velocity_body(
    Quat attitude_bi, Vec3 velocity_ecef_mps, Vec3 wind_velocity_ecef_mps,
    Vec3 *air_velocity_b_mps) {
    if (air_velocity_b_mps == nullptr) {
        return SIM_ERR_INVALID_ARG;
    }
    Matrix3 body_to_ecef;
    const SimStatus status = quat_to_dcm(attitude_bi, &body_to_ecef);
    if (status != SIM_OK) {
        return status;
    }
    *air_velocity_b_mps = matrix3_multiply_vec3(
        matrix3_transpose(body_to_ecef),
        vec3_sub(velocity_ecef_mps, wind_velocity_ecef_mps));
    return SIM_OK;
}

/* Mach from body-frame air velocity; zero sound speed maps to mach 0 the
   same way environment_force_model.c guards the division. */
C2CUDA_HOST_DEVICE inline double device_mach_from_air_velocity(
    Vec3 air_velocity_b_mps, double speed_of_sound_mps) {
    return speed_of_sound_mps > 0.0
        ? vec3_norm(air_velocity_b_mps) / speed_of_sound_mps
        : 0.0;
}

}  // namespace gpu_campaign

#endif
