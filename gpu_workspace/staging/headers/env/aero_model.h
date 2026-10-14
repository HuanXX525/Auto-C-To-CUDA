/** @file aero_model.h
 *  @brief 基础气动力和控制力矩模型。
 */
#ifndef ENV_AERO_MODEL_H
#define ENV_AERO_MODEL_H

#include "common/status.h"
#include "common/vec3.h"
#include "env/aero_database.h"
#include "env/aero_surrogate.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 气动模型配置。 */
typedef struct AeroModel {
    int enabled;
    double reference_area_m2;
    double reference_length_m;
    double drag_coefficient;
    double control_force_coefficient;
    double control_moment_coefficient;
    /** @brief 可选气动表；为空时使用简化二次阻力模型。 */
    AeroDatabase *database;
    /** @brief 可选只读代理模型；气动表为空时优先于简化阻力模型。 */
    AeroSurrogateModel *surrogate;
} AeroModel;

/** @brief 根据机体系相对气流和舵偏计算气动力、力矩。 */
SimStatus aero_model_evaluate(
    const AeroModel *model,
    double density_kgpm3,
    double mach,
    Vec3 velocity_air_b_mps,
    double pitch_actuator_rad,
    double yaw_actuator_rad,
    Vec3 *force_b_n,
    Vec3 *moment_b_nm,
    uint32_t *model_flags);

#ifdef __cplusplus
}
#endif

#endif
