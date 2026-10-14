/** @file matrix3.c
 *  @brief 3x3 矩阵基础运算实现。
 */
#include "common/matrix3.h"

#include <math.h>
#include <stddef.h>

/** @brief 构造所有元素均为零的矩阵。 */
Matrix3 matrix3_zero(void)
{
    Matrix3 result = { { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } } };
    return result;
}

/** @brief 构造三阶单位矩阵。 */
Matrix3 matrix3_identity(void)
{
    Matrix3 result = matrix3_zero();

    result.m[0][0] = 1.0;
    result.m[1][1] = 1.0;
    result.m[2][2] = 1.0;
    return result;
}

/** @brief 交换矩阵行列得到转置矩阵。 */
Matrix3 matrix3_transpose(Matrix3 matrix)
{
    Matrix3 result;
    size_t row;
    size_t column;

    for (row = 0u; row < 3u; ++row) {
        for (column = 0u; column < 3u; ++column) {
            result.m[row][column] = matrix.m[column][row];
        }
    }
    return result;
}

/** @brief 按标准行乘列规则计算矩阵乘积。 */
Matrix3 matrix3_multiply(Matrix3 left, Matrix3 right)
{
    Matrix3 result = matrix3_zero();
    size_t row;
    size_t column;
    size_t index;

    for (row = 0u; row < 3u; ++row) {
        for (column = 0u; column < 3u; ++column) {
            for (index = 0u; index < 3u; ++index) {
                result.m[row][column] += left.m[row][index] * right.m[index][column];
            }
        }
    }
    return result;
}

/** @brief 将输入向量视为列向量并执行矩阵左乘。 */
Vec3 matrix3_multiply_vec3(Matrix3 matrix, Vec3 vector)
{
    return vec3_make(
        (matrix.m[0][0] * vector.x) + (matrix.m[0][1] * vector.y) + (matrix.m[0][2] * vector.z),
        (matrix.m[1][0] * vector.x) + (matrix.m[1][1] * vector.y) + (matrix.m[1][2] * vector.z),
        (matrix.m[2][0] * vector.x) + (matrix.m[2][1] * vector.y) + (matrix.m[2][2] * vector.z));
}

/** @brief 使用三阶矩阵展开式计算行列式。 */
double matrix3_determinant(Matrix3 matrix)
{
    return
        (matrix.m[0][0] * ((matrix.m[1][1] * matrix.m[2][2]) - (matrix.m[1][2] * matrix.m[2][1]))) -
        (matrix.m[0][1] * ((matrix.m[1][0] * matrix.m[2][2]) - (matrix.m[1][2] * matrix.m[2][0]))) +
        (matrix.m[0][2] * ((matrix.m[1][0] * matrix.m[2][1]) - (matrix.m[1][1] * matrix.m[2][0])));
}

/** @brief 使用伴随矩阵和行列式计算三阶逆矩阵。 */
SimStatus matrix3_inverse(Matrix3 matrix, Matrix3 *out)
{
    double determinant;

    if (out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!matrix3_isfinite(matrix)) {
        return SIM_ERR_NUMERIC;
    }
    determinant = matrix3_determinant(matrix);
    if (!isfinite(determinant) || fabs(determinant) <= 1.0e-15) {
        return SIM_ERR_OUT_OF_RANGE;
    }

    out->m[0][0] = ((matrix.m[1][1] * matrix.m[2][2]) -
        (matrix.m[1][2] * matrix.m[2][1])) / determinant;
    out->m[0][1] = ((matrix.m[0][2] * matrix.m[2][1]) -
        (matrix.m[0][1] * matrix.m[2][2])) / determinant;
    out->m[0][2] = ((matrix.m[0][1] * matrix.m[1][2]) -
        (matrix.m[0][2] * matrix.m[1][1])) / determinant;
    out->m[1][0] = ((matrix.m[1][2] * matrix.m[2][0]) -
        (matrix.m[1][0] * matrix.m[2][2])) / determinant;
    out->m[1][1] = ((matrix.m[0][0] * matrix.m[2][2]) -
        (matrix.m[0][2] * matrix.m[2][0])) / determinant;
    out->m[1][2] = ((matrix.m[0][2] * matrix.m[1][0]) -
        (matrix.m[0][0] * matrix.m[1][2])) / determinant;
    out->m[2][0] = ((matrix.m[1][0] * matrix.m[2][1]) -
        (matrix.m[1][1] * matrix.m[2][0])) / determinant;
    out->m[2][1] = ((matrix.m[0][1] * matrix.m[2][0]) -
        (matrix.m[0][0] * matrix.m[2][1])) / determinant;
    out->m[2][2] = ((matrix.m[0][0] * matrix.m[1][1]) -
        (matrix.m[0][1] * matrix.m[1][0])) / determinant;
    return SIM_OK;
}

/** @brief 检查矩阵九个元素是否均为有限浮点数。 */
int matrix3_isfinite(Matrix3 matrix)
{
    size_t row;
    size_t column;

    for (row = 0u; row < 3u; ++row) {
        for (column = 0u; column < 3u; ++column) {
            if (!isfinite(matrix.m[row][column])) {
                return 0;
            }
        }
    }
    return 1;
}
