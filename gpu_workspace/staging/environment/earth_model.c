/** @file earth_model.c
 *  @brief 地球椭球参数实现。
 */
#include "env/earth_model.h"

#include <math.h>

/** @brief 构造标准 WGS-84 长半轴和扁率参数。 */
EarthModel earth_model_wgs84(void)
{
    EarthModel model = { 6378137.0, 1.0 / 298.257223563 };
    return model;
}

/** @brief 校验输入后构造自定义参考椭球。 */
SimStatus earth_model_make(double semi_major_axis_m, double flattening, EarthModel *out)
{
    EarthModel model;

    if (out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    model.semi_major_axis_m = semi_major_axis_m;
    model.flattening = flattening;
    if (earth_model_validate(&model) != SIM_OK) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    *out = model;
    return SIM_OK;
}

/** @brief 根据 e^2=f(2-f) 计算第一偏心率平方。 */
double earth_model_eccentricity_squared(const EarthModel *model)
{
    return model == 0 ? NAN : model->flattening * (2.0 - model->flattening);
}

/** @brief 根据 b=a(1-f) 计算短半轴。 */
double earth_model_semi_minor_axis(const EarthModel *model)
{
    return model == 0 ? NAN : model->semi_major_axis_m * (1.0 - model->flattening);
}

/** @brief 检查长半轴和扁率是否属于可计算范围。 */
SimStatus earth_model_validate(const EarthModel *model)
{
    if (model == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!isfinite(model->semi_major_axis_m) ||
        !isfinite(model->flattening) ||
        model->semi_major_axis_m <= 0.0 ||
        model->flattening < 0.0 ||
        model->flattening >= 1.0) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    return SIM_OK;
}
