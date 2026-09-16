/** @file atmosphere_model.c
 *  @brief ISA 对流层大气模型实现。
 */
#include "env/atmosphere_model.h"

#include <math.h>

/** @brief 构造 0-11 km ISA 对流层模型。 */
AtmosphereModel atmosphere_model_isa(void)
{
    AtmosphereModel model = { 1, 11000.0 };
    return model;
}

/** @brief 根据位势高度近似计算标准大气状态。 */
SimStatus atmosphere_model_evaluate(
    const AtmosphereModel *model,
    double height_m,
    AtmosphereState *out)
{
    const double sea_level_temperature = 288.15;
    const double sea_level_pressure = 101325.0;
    const double lapse_rate = 0.0065;
    const double gas_constant = 287.05287;
    const double gamma = 1.4;
    const double gravity = 9.80665;
    double altitude;

    if (model == 0 || out == 0 || !isfinite(height_m) ||
        !isfinite(model->maximum_model_height_m) ||
        model->maximum_model_height_m <= 0.0) {
        return SIM_ERR_INVALID_ARG;
    }
    altitude = height_m < 0.0 ? 0.0 : height_m;
    if (altitude > model->maximum_model_height_m) {
        altitude = model->maximum_model_height_m;
    }
    if (model->enabled == 0) {
        out->temperature_k = sea_level_temperature;
        out->pressure_pa = 0.0;
        out->density_kgpm3 = 0.0;
        out->speed_of_sound_mps = sqrt(gamma * gas_constant * sea_level_temperature);
        return SIM_OK;
    }
    out->temperature_k = sea_level_temperature - (lapse_rate * altitude);
    out->pressure_pa = sea_level_pressure *
        pow(out->temperature_k / sea_level_temperature, gravity / (gas_constant * lapse_rate));
    out->density_kgpm3 = out->pressure_pa / (gas_constant * out->temperature_k);
    out->speed_of_sound_mps = sqrt(gamma * gas_constant * out->temperature_k);
    return SIM_OK;
}
