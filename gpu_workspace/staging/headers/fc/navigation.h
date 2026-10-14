/** @file navigation.h
 *  @brief 导航解算状态。
 *
 *  该结构保存飞控侧对目标和自身运动学的估计结果，供制导律和控制分配
 *  使用。
 */
#ifndef FC_NAVIGATION_H
#define FC_NAVIGATION_H

#include "common/protocol.h"
#include "common/status.h"
#include "common/vec3.h"
#include "common/device_qualifiers.h"

#include <stdint.h>

/** @brief 导航状态中速度、加速度和角速度估计有效。 */
#define FC_NAV_VALID_KINEMATICS UINT32_C(0x00000001)
/** @brief 导航状态中导引头目标测量有效。 */
#define FC_NAV_VALID_SEEKER UINT32_C(0x00000002)
/** @brief 导航状态中经纬高和 AGL 测量有效。 */
#define FC_NAV_VALID_GEODETIC UINT32_C(0x00000004)

/** @brief 导航估计状态。 */
typedef struct NavState {
    /** @brief 当前仿真时间，单位秒。 */
    double time;
    /** @brief 导弹位置估计，ECEF 坐标，单位米。 */
    Vec3 missile_pos_ecef_est;
    /** @brief 导弹速度估计，ECEF 坐标，单位米每秒。 */
    Vec3 missile_vel_ecef_est;
    /** @brief 导弹加速度估计，ECEF 坐标。 */
    Vec3 missile_accel_ecef_est;
    /** @brief 导弹机体角速度估计。 */
    Vec3 omega_b_est;
    /** @brief 距离估计，单位米。 */
    double range;
    /** @brief 视线单位向量。 */
    Vec3 los_unit_ecef;
    /** @brief 视线角速度向量。 */
    Vec3 los_rate_ecef;
    /** @brief 闭合速度，单位米每秒。 */
    double closing_velocity;
    /** @brief 纬度估计，单位弧度。 */
    double lat_rad;
    /** @brief 经度估计，单位弧度。 */
    double lon_rad;
    /** @brief 高度估计，单位米。 */
    double height_m;
    /** @brief 离地高度估计，单位米。 */
    double height_agl_m;
    /** @brief 有效数据标志。 */
    uint32_t valid_flags;
} NavState;

/** @brief 将传感器帧转换为飞控导航状态。
 *
 *  @param sensor 环境程序发来的传感器帧，ECEF/LLA 单位遵循协议定义。
 *  @param out 调用方持有的导航状态输出。
 *
 *  该函数只使用传感器侧公开测量，不读取环境真值。无效通道不会写入
 *  对应 @c FC_NAV_VALID_* 位。
 */
C2CUDA_HOST_DEVICE SimStatus navigation_update_from_sensor(const SensorFrame *sensor, NavState *out);

/** @brief 判断导航状态是否具备比例导引所需的目标测量。 */
C2CUDA_HOST_DEVICE int navigation_has_guidance_solution(const NavState *nav);

#endif
