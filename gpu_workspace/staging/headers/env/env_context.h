/** @file env_context.h
 *  @brief 环境进程启动上下文。
 */
#ifndef ENV_ENV_CONTEXT_H
#define ENV_ENV_CONTEXT_H

#include <stdint.h>

/** @brief 环境仿真进程的启动参数。 */
typedef struct EnvContext {
    /** @brief 当前实例编号。 */
    uint32_t instance_id;
    /** @brief 场景配置文件路径。 */
    const char *scenario_path;
    /** @brief 运行时配置文件路径。 */
    const char *runtime_path;
    /** @brief 故障脚本配置文件路径。 */
    const char *faults_path;
    /** @brief 是否使用命令行传入的显式随机种子。 */
    int has_random_seed_override;
    /** @brief 实例显式随机种子；仅在 has_random_seed_override 非零时生效。 */
    uint64_t random_seed_override;
} EnvContext;

#endif
