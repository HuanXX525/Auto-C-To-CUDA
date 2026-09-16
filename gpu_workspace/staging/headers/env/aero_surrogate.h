/** @file aero_surrogate.h
 *  @brief 只读线性气动代理模型。
 */
#ifndef ENV_AERO_SURROGATE_H
#define ENV_AERO_SURROGATE_H

#include "common/status.h"
#include "common/device_qualifiers.h"

/** @brief 单个气动系数的线性代理项。 */
typedef struct AeroSurrogateLinearTerm {
    /** @brief 常数项。 */
    double offset;
    /** @brief Mach 系数。 */
    double mach;
    /** @brief 攻角系数，输入单位 rad。 */
    double alpha_rad;
    /** @brief 侧滑角系数，输入单位 rad。 */
    double beta_rad;
} AeroSurrogateLinearTerm;

/** @brief 代理模型输出的六个无量纲气动系数。 */
typedef struct AeroSurrogateCoefficients {
    double cx;
    double cy;
    double cz;
    double cl;
    double cm;
    double cn;
} AeroSurrogateCoefficients;

/** @brief 固定格式线性代理模型。 */
typedef struct AeroSurrogateModel {
    AeroSurrogateLinearTerm cx;
    AeroSurrogateLinearTerm cy;
    AeroSurrogateLinearTerm cz;
    AeroSurrogateLinearTerm cl;
    AeroSurrogateLinearTerm cm;
    AeroSurrogateLinearTerm cn;
} AeroSurrogateModel;

/** @brief 从文本文件加载线性代理模型。 */
SimStatus aero_surrogate_load_file(const char *path, AeroSurrogateModel *model);

/** @brief 根据 Mach、攻角和侧滑角推理六个气动系数。 */
C2CUDA_HOST_DEVICE SimStatus aero_surrogate_evaluate(
    const AeroSurrogateModel *model,
    double mach,
    double alpha_rad,
    double beta_rad,
    AeroSurrogateCoefficients *out);

#endif
