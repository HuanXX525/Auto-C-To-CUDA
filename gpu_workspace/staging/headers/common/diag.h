/** @file diag.h
 *  @brief 诊断标志位类型定义。
 *
 *  各模块通过位标志汇总运行时异常、退化和观测结果，便于生成摘要日志和
 *  决策降级策略。
 */
#ifndef COMMON_DIAG_H
#define COMMON_DIAG_H

#include <stdint.h>

/** @brief 统一诊断位集合。 */
typedef uint32_t SimDiagFlags;

#endif
