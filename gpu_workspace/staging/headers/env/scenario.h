/** @file scenario.h
 *  @brief 场景级基础参数。
 */
#ifndef ENV_SCENARIO_H
#define ENV_SCENARIO_H

/** @brief 仿真场景的时间参数。 */
typedef struct Scenario {
    /** @brief 环境和飞控的统一步长，单位秒。 */
    double dt;
    /** @brief 单次仿真的最大允许时长，单位秒。 */
    double max_time;
} Scenario;

#endif
