/** @file env_app.h
 *  @brief 环境仿真程序入口。
 *
 *  环境程序负责读取场景、维护真实状态、接收飞控指令，并向飞控输出
 *  传感器帧。该函数是环境进程的主驱动入口。
 */
#ifndef ENV_ENV_APP_H
#define ENV_ENV_APP_H

#include "env/env_context.h"
#include "common/status.h"

/** @brief 运行指定实例的环境仿真主循环。 */
SimStatus env_app_run(const EnvContext *ctx);

#endif
