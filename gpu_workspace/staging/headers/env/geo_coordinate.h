/** @file geo_coordinate.h
 *  @brief 地理坐标与地心坐标表示。
 */
#ifndef ENV_GEO_COORDINATE_H
#define ENV_GEO_COORDINATE_H

#include "common/matrix3.h"
#include "common/status.h"
#include "common/vec3.h"
#include "env/earth_model.h"
#include "common/device_qualifiers.h"

/** @brief 大地经纬高坐标。 */
typedef struct LlaCoord {
    /** @brief 纬度，单位弧度。 */
    double lat_rad;
    /** @brief 经度，单位弧度。 */
    double lon_rad;
    /** @brief 椭球高或球高，单位米。 */
    double height_m;
} LlaCoord;

/** @brief 地心地固坐标。 */
typedef struct EcefCoord {
    /** @brief 空间位置向量，单位米。 */
    Vec3 position_m;
} EcefCoord;

/** @brief 将大地经纬高转换为 ECEF。 */
C2CUDA_HOST_DEVICE SimStatus geo_lla_to_ecef(
    const EarthModel *earth,
    const LlaCoord *lla,
    EcefCoord *out);
/** @brief 将 ECEF 转换为大地经纬高。 */
C2CUDA_HOST_DEVICE SimStatus geo_ecef_to_lla(
    const EarthModel *earth,
    const EcefCoord *ecef,
    LlaCoord *out);
/** @brief 计算 ECEF 向量到局部 ENU 的旋转矩阵。 */
C2CUDA_HOST_DEVICE SimStatus geo_ecef_to_enu_matrix(const LlaCoord *reference, Matrix3 *out);
/** @brief 计算 ECEF 向量到局部 NED 的旋转矩阵。 */
C2CUDA_HOST_DEVICE SimStatus geo_ecef_to_ned_matrix(const LlaCoord *reference, Matrix3 *out);
/** @brief 将 ECEF 位置转换为相对参考点的 ENU 位置。 */
C2CUDA_HOST_DEVICE SimStatus geo_ecef_position_to_enu(
    const EarthModel *earth,
    const LlaCoord *reference,
    const EcefCoord *position,
    Vec3 *out);
/** @brief 将 ECEF 位置转换为相对参考点的 NED 位置。 */
C2CUDA_HOST_DEVICE SimStatus geo_ecef_position_to_ned(
    const EarthModel *earth,
    const LlaCoord *reference,
    const EcefCoord *position,
    Vec3 *out);

#endif
