/** @file propulsion_model.h
 *  @brief 定推力固体发动机基础模型。
 */
#ifndef ENV_PROPULSION_MODEL_H
#define ENV_PROPULSION_MODEL_H

#include "common/status.h"
#include "common/vec3.h"

/** @brief 推进模型配置。 */
typedef struct PropulsionModel {
    int enabled;
    /** @brief 发动机点火后恒定推力，单位牛。 */
    double thrust_n;
    /** @brief 推进剂质量流量，单位 kg/s。 */
    double mass_flow_kgps;
    /** @brief 推力持续时间，单位秒。 */
    double burn_time_s;
    /** @brief 机体系单位推力方向。 */
    Vec3 thrust_direction_b;
} PropulsionModel;

/** @brief 计算当前时刻推力和质量流量。 */
SimStatus propulsion_model_evaluate(
    const PropulsionModel *model,
    double time_s,
    Vec3 *force_b_n,
    double *mass_flow_kgps);

#endif
