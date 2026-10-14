/** @file sim_time.h
 *  @brief 仿真时基与步进工具。
 *
 *  仿真调度统一以离散步进驱动。该接口把 tick 和步长绑定起来，供环境
 *  与飞控共享相同的仿真时钟语义。
 */
#ifndef COMMON_SIM_TIME_H
#define COMMON_SIM_TIME_H

#include <stdint.h>

/** @brief 仿真时间状态。 */
typedef struct SimTime {
    uint64_t tick;
    double dt;
} SimTime;

/** @brief 将当前 tick 转换为秒。 */
double sim_time_seconds(SimTime time);
/** @brief 推进一个离散仿真步。 */
void sim_time_step(SimTime *time);

#endif
