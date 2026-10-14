/** @file math_constants.h
 *  @brief 全工程共享的数学常量与角度换算因子。
 */
#ifndef COMMON_MATH_CONSTANTS_H
#define COMMON_MATH_CONSTANTS_H

/** @brief 圆周率。 */
#define SIM_PI 3.14159265358979323846
/** @brief 两倍圆周率。 */
#define SIM_TWO_PI 6.28318530717958647692
/** @brief 角度转弧度比例。 */
#define SIM_DEG_TO_RAD (SIM_PI / 180.0)
/** @brief 弧度转角度比例。 */
#define SIM_RAD_TO_DEG (180.0 / SIM_PI)

#endif
