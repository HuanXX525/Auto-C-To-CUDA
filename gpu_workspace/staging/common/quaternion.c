/** @file quaternion.c
 *  @brief 姿态四元数运算实现。
 */
#include "common/quaternion.h"

#include <math.h>

/** @brief 构造零旋转对应的单位四元数。 */
Quat quat_identity(void)
{
    Quat result = { 1.0, 0.0, 0.0, 0.0 };
    return result;
}

/** @brief 检查四元数所有分量是否均为有限数。 */
int quat_isfinite(Quat quat)
{
    return isfinite(quat.w) && isfinite(quat.x) && isfinite(quat.y) && isfinite(quat.z);
}

/** @brief 将非零四元数缩放到单位模。 */
SimStatus quat_normalize(Quat quat, Quat *out)
{
    double norm;

    if (out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!quat_isfinite(quat)) {
        return SIM_ERR_NUMERIC;
    }

    norm = sqrt(
        (quat.w * quat.w) +
        (quat.x * quat.x) +
        (quat.y * quat.y) +
        (quat.z * quat.z));
    if (norm <= 1.0e-15) {
        return SIM_ERR_OUT_OF_RANGE;
    }

    out->w = quat.w / norm;
    out->x = quat.x / norm;
    out->y = quat.y / norm;
    out->z = quat.z / norm;
    return SIM_OK;
}

/** @brief 计算 Hamilton 积；乘法顺序决定旋转复合顺序。 */
Quat quat_multiply(Quat left, Quat right)
{
    Quat result;

    result.w = (left.w * right.w) - (left.x * right.x) -
        (left.y * right.y) - (left.z * right.z);
    result.x = (left.w * right.x) + (left.x * right.w) +
        (left.y * right.z) - (left.z * right.y);
    result.y = (left.w * right.y) - (left.x * right.z) +
        (left.y * right.w) + (left.z * right.x);
    result.z = (left.w * right.z) + (left.x * right.y) -
        (left.y * right.x) + (left.z * right.w);
    return result;
}

/** @brief 将归一化四元数转换为方向余弦矩阵。 */
SimStatus quat_to_dcm(Quat quat, Matrix3 *out)
{
    Quat q;
    SimStatus status;

    if (out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    status = quat_normalize(quat, &q);
    if (status != SIM_OK) {
        return status;
    }

    out->m[0][0] = 1.0 - (2.0 * ((q.y * q.y) + (q.z * q.z)));
    out->m[0][1] = 2.0 * ((q.x * q.y) - (q.w * q.z));
    out->m[0][2] = 2.0 * ((q.x * q.z) + (q.w * q.y));
    out->m[1][0] = 2.0 * ((q.x * q.y) + (q.w * q.z));
    out->m[1][1] = 1.0 - (2.0 * ((q.x * q.x) + (q.z * q.z)));
    out->m[1][2] = 2.0 * ((q.y * q.z) - (q.w * q.x));
    out->m[2][0] = 2.0 * ((q.x * q.z) - (q.w * q.y));
    out->m[2][1] = 2.0 * ((q.y * q.z) + (q.w * q.x));
    out->m[2][2] = 1.0 - (2.0 * ((q.x * q.x) + (q.y * q.y)));
    return SIM_OK;
}

/** @brief 根据机体系角速度构造旋转增量并推进姿态。
 *
 *  @param quat 当前姿态四元数。
 *  @param omega_body_radps 机体系角速度，单位 rad/s。
 *  @param dt 积分步长，单位 s，必须非负。
 *  @param out 归一化后的下一时刻姿态。
 */
SimStatus quat_integrate(Quat quat, Vec3 omega_body_radps, double dt, Quat *out)
{
    double omega_norm;
    double half_angle;
    double scale;
    Quat delta;
    Quat advanced;

    if (out == 0 || !isfinite(dt)) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!quat_isfinite(quat) || !vec3_isfinite(omega_body_radps)) {
        return SIM_ERR_NUMERIC;
    }
    if (dt < 0.0) {
        return SIM_ERR_OUT_OF_RANGE;
    }

    omega_norm = vec3_norm(omega_body_radps);
    half_angle = 0.5 * omega_norm * dt;
    if (omega_norm > 1.0e-12) {
        scale = sin(half_angle) / omega_norm;
        delta.w = cos(half_angle);
        delta.x = omega_body_radps.x * scale;
        delta.y = omega_body_radps.y * scale;
        delta.z = omega_body_radps.z * scale;
    } else {
        delta.w = 1.0;
        delta.x = 0.5 * omega_body_radps.x * dt;
        delta.y = 0.5 * omega_body_radps.y * dt;
        delta.z = 0.5 * omega_body_radps.z * dt;
    }

    advanced = quat_multiply(quat, delta);
    return quat_normalize(advanced, out);
}
