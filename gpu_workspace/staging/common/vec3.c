/** @file vec3.c
 *  @brief 三维向量基础运算实现。
 */
#include "common/vec3.h"

#include <math.h>

/** @brief 构造向量。 */
Vec3 vec3_make(double x, double y, double z)
{
    Vec3 v = { x, y, z };
    return v;
}

/** @brief 向量加法。 */
Vec3 vec3_add(Vec3 a, Vec3 b)
{
    return vec3_make(a.x + b.x, a.y + b.y, a.z + b.z);
}

/** @brief 向量减法。 */
Vec3 vec3_sub(Vec3 a, Vec3 b)
{
    return vec3_make(a.x - b.x, a.y - b.y, a.z - b.z);
}

/** @brief 向量数乘。 */
Vec3 vec3_scale(Vec3 v, double scale)
{
    return vec3_make(v.x * scale, v.y * scale, v.z * scale);
}

/** @brief 向量点积。 */
double vec3_dot(Vec3 a, Vec3 b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

/** @brief 向量叉积。 */
Vec3 vec3_cross(Vec3 a, Vec3 b)
{
    return vec3_make(
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x));
}

/** @brief 向量模长。 */
double vec3_norm(Vec3 v)
{
    return sqrt(vec3_dot(v, v));
}

/** @brief 判断是否为有限向量。 */
int vec3_isfinite(Vec3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

/** @brief 归一化向量。 */
SimStatus vec3_normalize(Vec3 v, Vec3 *out)
{
    double n;

    if (out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!vec3_isfinite(v)) {
        return SIM_ERR_NUMERIC;
    }

    n = vec3_norm(v);
    if (n <= 0.0) {
        return SIM_ERR_OUT_OF_RANGE;
    }

    *out = vec3_scale(v, 1.0 / n);
    return SIM_OK;
}
