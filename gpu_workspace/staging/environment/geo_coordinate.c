/** @file geo_coordinate.c
 *  @brief WGS-84 大地坐标、ECEF 和局部坐标转换。
 */
#include "env/geo_coordinate.h"

#include "common/math_constants.h"

#include <math.h>

/** @brief 校验经纬度范围、高度和浮点有效性。 */
static SimStatus validate_lla(const LlaCoord *lla)
{
    if (lla == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!isfinite(lla->lat_rad) || !isfinite(lla->lon_rad) || !isfinite(lla->height_m)) {
        return SIM_ERR_NUMERIC;
    }
    if (lla->lat_rad < (-0.5 * SIM_PI) || lla->lat_rad > (0.5 * SIM_PI) ||
        lla->lon_rad < -SIM_PI || lla->lon_rad > SIM_PI) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    return SIM_OK;
}

/** @brief 按 WGS-84 卯酉圈曲率公式将 LLA 转换为 ECEF。 */
SimStatus geo_lla_to_ecef(
    const EarthModel *earth,
    const LlaCoord *lla,
    EcefCoord *out)
{
    double e2;
    double sin_lat;
    double cos_lat;
    double radius;
    SimStatus status;

    if (out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    status = earth_model_validate(earth);
    if (status != SIM_OK) {
        return status;
    }
    status = validate_lla(lla);
    if (status != SIM_OK) {
        return status;
    }

    e2 = earth_model_eccentricity_squared(earth);
    sin_lat = sin(lla->lat_rad);
    cos_lat = cos(lla->lat_rad);
    radius = earth->semi_major_axis_m / sqrt(1.0 - (e2 * sin_lat * sin_lat));
    out->position_m = vec3_make(
        (radius + lla->height_m) * cos_lat * cos(lla->lon_rad),
        (radius + lla->height_m) * cos_lat * sin(lla->lon_rad),
        ((radius * (1.0 - e2)) + lla->height_m) * sin_lat);
    return vec3_isfinite(out->position_m) ? SIM_OK : SIM_ERR_NUMERIC;
}

/** @brief 使用 Bowring 公式将 ECEF 转换为大地经纬高。
 *
 *  地球中心没有唯一经纬度，因此该输入返回范围错误；极点采用专门分支，
 *  避免高度计算中的 cos(latitude) 奇异性。
 */
SimStatus geo_ecef_to_lla(
    const EarthModel *earth,
    const EcefCoord *ecef,
    LlaCoord *out)
{
    double a;
    double b;
    double e2;
    double ep2;
    double p;
    double theta;
    double sin_theta;
    double cos_theta;
    double sin_lat;
    double radius;
    SimStatus status;

    if (ecef == 0 || out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    status = earth_model_validate(earth);
    if (status != SIM_OK) {
        return status;
    }
    if (!vec3_isfinite(ecef->position_m)) {
        return SIM_ERR_NUMERIC;
    }

    a = earth->semi_major_axis_m;
    b = earth_model_semi_minor_axis(earth);
    e2 = earth_model_eccentricity_squared(earth);
    ep2 = ((a * a) - (b * b)) / (b * b);
    p = hypot(ecef->position_m.x, ecef->position_m.y);
    if (p < 1.0e-9) {
        if (fabs(ecef->position_m.z) < 1.0e-9) {
            return SIM_ERR_OUT_OF_RANGE;
        }
        out->lat_rad = ecef->position_m.z >= 0.0 ? 0.5 * SIM_PI : -0.5 * SIM_PI;
        out->lon_rad = 0.0;
        out->height_m = fabs(ecef->position_m.z) - b;
        return SIM_OK;
    }

    theta = atan2(ecef->position_m.z * a, p * b);
    sin_theta = sin(theta);
    cos_theta = cos(theta);
    out->lat_rad = atan2(
        ecef->position_m.z + (ep2 * b * sin_theta * sin_theta * sin_theta),
        p - (e2 * a * cos_theta * cos_theta * cos_theta));
    out->lon_rad = atan2(ecef->position_m.y, ecef->position_m.x);
    sin_lat = sin(out->lat_rad);
    radius = a / sqrt(1.0 - (e2 * sin_lat * sin_lat));
    out->height_m = p / cos(out->lat_rad) - radius;
    return validate_lla(out);
}

/** @brief 构造参考点处 ECEF 到 East-North-Up 的旋转矩阵。 */
SimStatus geo_ecef_to_enu_matrix(const LlaCoord *reference, Matrix3 *out)
{
    double sin_lat;
    double cos_lat;
    double sin_lon;
    double cos_lon;
    SimStatus status;

    if (out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    status = validate_lla(reference);
    if (status != SIM_OK) {
        return status;
    }

    sin_lat = sin(reference->lat_rad);
    cos_lat = cos(reference->lat_rad);
    sin_lon = sin(reference->lon_rad);
    cos_lon = cos(reference->lon_rad);
    out->m[0][0] = -sin_lon;
    out->m[0][1] = cos_lon;
    out->m[0][2] = 0.0;
    out->m[1][0] = -sin_lat * cos_lon;
    out->m[1][1] = -sin_lat * sin_lon;
    out->m[1][2] = cos_lat;
    out->m[2][0] = cos_lat * cos_lon;
    out->m[2][1] = cos_lat * sin_lon;
    out->m[2][2] = sin_lat;
    return SIM_OK;
}

/** @brief 由 ENU 轴交换和 Up 轴反向构造 North-East-Down 矩阵。 */
SimStatus geo_ecef_to_ned_matrix(const LlaCoord *reference, Matrix3 *out)
{
    Matrix3 enu;
    SimStatus status;

    if (out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    status = geo_ecef_to_enu_matrix(reference, &enu);
    if (status != SIM_OK) {
        return status;
    }
    out->m[0][0] = enu.m[1][0];
    out->m[0][1] = enu.m[1][1];
    out->m[0][2] = enu.m[1][2];
    out->m[1][0] = enu.m[0][0];
    out->m[1][1] = enu.m[0][1];
    out->m[1][2] = enu.m[0][2];
    out->m[2][0] = -enu.m[2][0];
    out->m[2][1] = -enu.m[2][1];
    out->m[2][2] = -enu.m[2][2];
    return SIM_OK;
}

/** @brief 将 ECEF 位置差旋转到指定局部坐标系。 */
static SimStatus position_to_local(
    const EarthModel *earth,
    const LlaCoord *reference,
    const EcefCoord *position,
    int use_ned,
    Vec3 *out)
{
    EcefCoord origin;
    Matrix3 rotation;
    SimStatus status;

    if (position == 0 || out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    status = geo_lla_to_ecef(earth, reference, &origin);
    if (status != SIM_OK) {
        return status;
    }
    status = use_ned != 0 ?
        geo_ecef_to_ned_matrix(reference, &rotation) :
        geo_ecef_to_enu_matrix(reference, &rotation);
    if (status != SIM_OK) {
        return status;
    }
    *out = matrix3_multiply_vec3(
        rotation,
        vec3_sub(position->position_m, origin.position_m));
    return vec3_isfinite(*out) ? SIM_OK : SIM_ERR_NUMERIC;
}

/** @brief 将绝对 ECEF 位置转换为参考点局部 ENU 坐标。 */
SimStatus geo_ecef_position_to_enu(
    const EarthModel *earth,
    const LlaCoord *reference,
    const EcefCoord *position,
    Vec3 *out)
{
    return position_to_local(earth, reference, position, 0, out);
}

/** @brief 将绝对 ECEF 位置转换为参考点局部 NED 坐标。 */
SimStatus geo_ecef_position_to_ned(
    const EarthModel *earth,
    const LlaCoord *reference,
    const EcefCoord *position,
    Vec3 *out)
{
    return position_to_local(earth, reference, position, 1, out);
}
