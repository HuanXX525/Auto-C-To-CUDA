/** @file atmosphere_model.h
 *  @brief ISA 对流层大气模型。
 */
#ifndef ENV_ATMOSPHERE_MODEL_H
#define ENV_ATMOSPHERE_MODEL_H

#include "common/status.h"
#include "common/device_qualifiers.h"

/** @brief 单点大气状态。 */
typedef struct AtmosphereState {
    double temperature_k;
    double pressure_pa;
    double density_kgpm3;
    double speed_of_sound_mps;
} AtmosphereState;

/** @brief 大气模型配置。 */
typedef struct AtmosphereModel {
    int enabled;
    /** @brief 超出标准对流层后用于计算的最大高度。 */
    double maximum_model_height_m;
} AtmosphereModel;

/** @brief 返回标准 ISA 对流层配置。 */
C2CUDA_HOST_DEVICE AtmosphereModel atmosphere_model_isa(void);
/** @brief 根据椭球高近似计算温度、压力、密度和声速。 */
C2CUDA_HOST_DEVICE SimStatus atmosphere_model_evaluate(
    const AtmosphereModel *model,
    double height_m,
    AtmosphereState *out);

#endif
