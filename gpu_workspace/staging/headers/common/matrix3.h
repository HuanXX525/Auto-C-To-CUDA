/** @file matrix3.h
 *  @brief 3x3 矩阵数据结构与基础运算。
 *
 *  矩阵按行主序存储，主要用于方向余弦矩阵、惯性矩阵和坐标系变换。
 *  接口不分配内存，适合在确定性仿真主循环中使用。
 */
#ifndef COMMON_MATRIX3_H
#define COMMON_MATRIX3_H

#include "common/vec3.h"
#include "common/device_qualifiers.h"

/** @brief 3x3 实矩阵，按行主序存储。 */
typedef struct Matrix3 {
    double m[3][3];
} Matrix3;

/** @brief 返回三阶单位矩阵。 */
C2CUDA_HOST_DEVICE Matrix3 matrix3_identity(void);
/** @brief 返回三阶零矩阵。 */
C2CUDA_HOST_DEVICE Matrix3 matrix3_zero(void);
/** @brief 计算矩阵转置。 */
C2CUDA_HOST_DEVICE Matrix3 matrix3_transpose(Matrix3 matrix);
/** @brief 计算两个三阶矩阵的乘积。 */
C2CUDA_HOST_DEVICE Matrix3 matrix3_multiply(Matrix3 left, Matrix3 right);
/** @brief 计算矩阵与列向量的乘积。 */
C2CUDA_HOST_DEVICE Vec3 matrix3_multiply_vec3(Matrix3 matrix, Vec3 vector);
/** @brief 计算三阶矩阵行列式。 */
C2CUDA_HOST_DEVICE double matrix3_determinant(Matrix3 matrix);
/** @brief 计算可逆三阶矩阵的逆矩阵。 */
C2CUDA_HOST_DEVICE SimStatus matrix3_inverse(Matrix3 matrix, Matrix3 *out);
/** @brief 判断矩阵所有元素是否均为有限数。 */
C2CUDA_HOST_DEVICE int matrix3_isfinite(Matrix3 matrix);

#endif
