/** @file mass_model.h
 *  @brief 干质量、推进剂质量和质量流量模型。
 */
#ifndef ENV_MASS_MODEL_H
#define ENV_MASS_MODEL_H

#include "common/status.h"

/** @brief 质量模型参数。 */
typedef struct MassModel {
    double mass_kg;
    /** @brief 不可消耗干质量，单位千克。 */
    double dry_mass_kg;
    /** @brief 当前推进剂质量，单位千克。 */
    double propellant_mass_kg;
} MassModel;

/** @brief 初始化干质量和推进剂质量。 */
SimStatus mass_model_init(MassModel *model, double dry_mass_kg, double propellant_mass_kg);
/** @brief 按质量流量消耗推进剂并更新总质量。 */
SimStatus mass_model_step(MassModel *model, double mass_flow_kgps, double dt);

#endif
