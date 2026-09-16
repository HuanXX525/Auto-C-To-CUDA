/** @file guidance_manager.h
 *  @brief 制导管理器。
 *
 *  制导管理器负责把导航估计转换为具体制导律输入，并统一处理制导律
 *  错误计数。当前主制导律为三维比例导引 PNG。
 */
#ifndef FC_GUIDANCE_MANAGER_H
#define FC_GUIDANCE_MANAGER_H

#include "common/status.h"
#include "fc/guidance_png.h"
#include "fc/navigation.h"
#include "common/device_qualifiers.h"

#include <stdint.h>

/** @brief 制导管理器状态。
 *
 *  @var png_config
 *  三维比例导引配置，单位和范围遵循 @c GuidancePngConfig。
 */
typedef struct GuidanceManager {
    GuidancePngConfig png_config;
    uint32_t accepted_count;
    uint32_t rejected_count;
} GuidanceManager;

/** @brief 初始化制导管理器。 */
C2CUDA_HOST_DEVICE SimStatus guidance_manager_init(GuidanceManager *manager, const GuidancePngConfig *png_config);

/** @brief 根据导航估计执行一次制导计算。 */
C2CUDA_HOST_DEVICE SimStatus guidance_manager_update(
    GuidanceManager *manager,
    const NavState *nav,
    GuidancePngOutput *out);

#endif
