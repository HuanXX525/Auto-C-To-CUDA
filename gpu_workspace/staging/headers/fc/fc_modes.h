/** @file fc_modes.h
 *  @brief 飞控工作模式和状态机转换接口。
 *
 *  模式状态机会随每个传感器帧推进。非硬件仿真中自检状态为软件自检，
 *  主要检查配置、输入有限性和制导链路是否可用。
 */
#ifndef FC_FC_MODES_H
#define FC_FC_MODES_H

#include "common/device_qualifiers.h"

/** @brief 飞控状态机模式。 */
typedef enum FcMode {
    FC_POWER_ON = 0,
    FC_SELF_TEST,
    FC_WAIT_SENSOR,
    FC_NAV_READY,
    FC_GUIDANCE_STANDBY,
    FC_GUIDANCE_ACTIVE,
    FC_COMMAND_HOLD,
    FC_DEGRADED,
    FC_FAULT,
    FC_SHUTDOWN
} FcMode;

/** @brief 单帧状态机输入。
 *
 *  所有字段均为布尔语义。调用方在完成传感器校验、导航估计、制导计算
 *  和安全监视后填写该结构，再由状态机决定下一模式。
 */
typedef struct FcModeInput {
    /** @brief 软件自检是否通过。 */
    int self_test_ok;
    /** @brief 本帧传感器数据是否可被飞控接受。 */
    int sensor_frame_valid;
    /** @brief 导航/估计结果是否满足下游使用条件。 */
    int navigation_valid;
    /** @brief 本帧是否具备可用的目标制导解。 */
    int guidance_valid;
    /** @brief 是否需要进入命令保持或受控零输出。 */
    int command_hold;
    /** @brief 是否处于降级但仍可受控运行。 */
    int degraded;
    /** @brief 是否出现飞控故障。 */
    int fault;
    /** @brief 是否请求停机。 */
    int shutdown;
} FcModeInput;

/** @brief 计算飞控状态机的下一模式。 */
C2CUDA_HOST_DEVICE FcMode fc_mode_next(FcMode current, const FcModeInput *input);

/** @brief 返回飞控模式的静态可读名称。 */
C2CUDA_HOST_DEVICE const char *fc_mode_to_string(FcMode mode);

#endif
