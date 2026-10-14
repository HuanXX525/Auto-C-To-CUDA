/** @file guidance_png.h
 *  @brief 比例导引法制导接口。
 *
 *  该模块根据目标视线、视线角速度和闭合速度生成加速度指令，是飞控制导
 *  核心算法的主要入口。
 */
#ifndef FC_GUIDANCE_PNG_H
#define FC_GUIDANCE_PNG_H

#include "common/status.h"
#include "common/vec3.h"
#include "common/device_qualifiers.h"

/** @brief PNG 制导参数。
 *
 *  @var navigation_constant
 *  比例导引导航比 N，无量纲，必须为正有限数。
 *  @var max_accel_mps2
 *  期望加速度范数上限，单位 m/s^2，必须为非负有限数。
 *  @var max_accel_rate_mps3
 *  指令变化率上限，单位 m/s^3，由命令管理器使用。
 */
typedef struct GuidancePngConfig {
    /** @brief 导引系数。 */
    double navigation_constant;
    /** @brief 最大加速度限幅。 */
    double max_accel_mps2;
    /** @brief 最大加速度变化率限幅。 */
    double max_accel_rate_mps3;
} GuidancePngConfig;

/** @brief PNG 制导输入量。 */
typedef struct GuidancePngInput {
    /** @brief 当前目标距离，单位米。 */
    double range_m;
    /** @brief 闭合速度，单位米每秒。 */
    double closing_velocity_mps;
    /** @brief 视线单位向量，ECEF 坐标系。 */
    Vec3 los_unit_ecef;
    /** @brief 视线角速度向量，ECEF 坐标系。 */
    Vec3 los_rate_ecef;
} GuidancePngInput;

/** @brief PNG 制导输出量。 */
typedef struct GuidancePngOutput {
    /** @brief 生成的加速度指令，ECEF 坐标系。 */
    Vec3 accel_cmd_ecef;
} GuidancePngOutput;

/** @brief 执行一次 PNG 制导更新。
 *
 *  使用三维比例导引公式
 *  @f$ \mathbf a_c = N V_c(\boldsymbol\omega_{LOS}\times\hat{\mathbf r}) @f$。
 *  输入和输出均位于 ECEF 坐标系。若距离、闭合速度或有限性检查失败，
 *  函数返回错误并不产生可用指令。
 */
C2CUDA_HOST_DEVICE SimStatus guidance_png_update(
    const GuidancePngConfig *cfg,
    const GuidancePngInput *in,
    GuidancePngOutput *out);

#endif
