/** @file world_state.h
 *  @brief 环境仿真的实时世界状态。
 */
#ifndef ENV_WORLD_STATE_H
#define ENV_WORLD_STATE_H

#include "common/protocol.h"

/** @brief 环境层保存的当前世界状态快照。 */
typedef struct WorldState {
    /** @brief 当前仿真时间，单位秒。 */
    double sim_time;
    /** @brief 最新一次向飞控发布的传感器帧。 */
    SensorFrame sensor_frame;
    /** @brief 最近一次接收到的控制指令。 */
    ControlCommand control_command;
} WorldState;

#endif
