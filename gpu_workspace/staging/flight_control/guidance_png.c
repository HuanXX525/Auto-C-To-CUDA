/** @file guidance_png.c
 *  @brief 比例导引法制导实现。
 */
#include "fc/guidance_png.h"

#include <math.h>

/** @brief 计算 PNG 制导加速度指令。 */
SimStatus guidance_png_update(
    const GuidancePngConfig *cfg,
    const GuidancePngInput *in,
    GuidancePngOutput *out)
{
    Vec3 direction;
    Vec3 accel;
    double norm;

    if (cfg == 0 || in == 0 || out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    out->accel_cmd_ecef = vec3_make(0.0, 0.0, 0.0);
    if (!isfinite(cfg->navigation_constant) ||
        !isfinite(cfg->max_accel_mps2) ||
        !isfinite(cfg->max_accel_rate_mps3) ||
        !isfinite(in->range_m) ||
        !isfinite(in->closing_velocity_mps) ||
        !vec3_isfinite(in->los_unit_ecef) ||
        !vec3_isfinite(in->los_rate_ecef)) {
        return SIM_ERR_NUMERIC;
    }
    if (in->range_m <= 0.0 ||
        in->closing_velocity_mps <= 0.0 ||
        cfg->navigation_constant <= 0.0 ||
        cfg->max_accel_mps2 < 0.0 ||
        cfg->max_accel_rate_mps3 < 0.0) {
        return SIM_ERR_OUT_OF_RANGE;
    }

    direction = vec3_cross(in->los_rate_ecef, in->los_unit_ecef);
    accel = vec3_scale(direction, cfg->navigation_constant * in->closing_velocity_mps);
    norm = vec3_norm(accel);
    if (norm > cfg->max_accel_mps2 && norm > 0.0) {
        accel = vec3_scale(accel, cfg->max_accel_mps2 / norm);
    }

    out->accel_cmd_ecef = accel;
    return SIM_OK;
}
