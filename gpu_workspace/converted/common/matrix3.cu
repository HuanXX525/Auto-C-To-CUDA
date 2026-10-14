/** @file matrix3.c
 *  @brief 3x3 矩阵基础运算实现。
 */
#include "common/matrix3.h"
#include <math.h>
#include <stddef.h>
/** @brief 构造所有元素均为零的矩阵。 */
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

Matrix3 matrix3_zero()
{
  Matrix3 result = {/* Need explicit braces: is this where we insert the class name? */ {/* Need explicit braces: is this where we insert the class name? */ {(0.0), (0.0), (0.0)}, /* Need explicit braces: is this where we insert the class name? */ {(0.0), (0.0), (0.0)}, /* Need explicit braces: is this where we insert the class name? */ {(0.0), (0.0), (0.0)}}};
  return result;
}
/** @brief 构造三阶单位矩阵。 */

Matrix3 matrix3_identity()
{
  Matrix3 result = matrix3_zero();
  result . m[0][0] = 1.0;
  result . m[1][1] = 1.0;
  result . m[2][2] = 1.0;
  return result;
}
/** @brief 交换矩阵行列得到转置矩阵。 */

Matrix3 matrix3_transpose(Matrix3 matrix)
{
  Matrix3 result;
  int row;
  int column;
  for (row = 1; row <= (((unsigned long )3u) + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; row += 1) {
    for (column = 1; column <= (((unsigned long )3u) + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; column += 1) {
      result . m[1 * row + -1LL][1 * column + -1LL] = matrix . m[1 * column + -1LL][1 * row + -1LL];
    }
  }
  return result;
}
/** @brief 按标准行乘列规则计算矩阵乘积。 */

Matrix3 matrix3_multiply(Matrix3 left,Matrix3 right)
{
  Matrix3 result = matrix3_zero();
  int row;
  int column;
  int index;
  for (row = 1; row <= (((unsigned long )3u) + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; row += 1) {
    for (column = 1; column <= (((unsigned long )3u) + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; column += 1) {
      for (index = 1; index <= (((unsigned long )3u) + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; index += 1) {
        result . m[1 * row + -1LL][1 * column + -1LL] += left . m[1 * row + -1LL][1 * index + -1LL] * right . m[1 * index + -1LL][1 * column + -1LL];
      }
    }
  }
  return result;
}
/** @brief 将输入向量视为列向量并执行矩阵左乘。 */

Vec3 matrix3_multiply_vec3(Matrix3 matrix,Vec3 vector)
{
  return vec3_make(matrix . m[0][0] * vector . x + matrix . m[0][1] * vector . y + matrix . m[0][2] * vector . z,matrix . m[1][0] * vector . x + matrix . m[1][1] * vector . y + matrix . m[1][2] * vector . z,matrix . m[2][0] * vector . x + matrix . m[2][1] * vector . y + matrix . m[2][2] * vector . z);
}
/** @brief 使用三阶矩阵展开式计算行列式。 */

double matrix3_determinant(Matrix3 matrix)
{
  return matrix . m[0][0] * (matrix . m[1][1] * matrix . m[2][2] - matrix . m[1][2] * matrix . m[2][1]) - matrix . m[0][1] * (matrix . m[1][0] * matrix . m[2][2] - matrix . m[1][2] * matrix . m[2][0]) + matrix . m[0][2] * (matrix . m[1][0] * matrix . m[2][1] - matrix . m[1][1] * matrix . m[2][0]);
}
/** @brief 使用伴随矩阵和行列式计算三阶逆矩阵。 */

SimStatus matrix3_inverse(Matrix3 matrix,Matrix3 *out)
{
  double determinant;
  if (out == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (!matrix3_isfinite(matrix)) {
    return SIM_ERR_NUMERIC;
  }
  determinant = matrix3_determinant(matrix);
  if (!__builtin_isfinite(determinant) || fabs(determinant) <= 1.0e-15) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  out -> m[0][0] = (matrix . m[1][1] * matrix . m[2][2] - matrix . m[1][2] * matrix . m[2][1]) / determinant;
  out -> m[0][1] = (matrix . m[0][2] * matrix . m[2][1] - matrix . m[0][1] * matrix . m[2][2]) / determinant;
  out -> m[0][2] = (matrix . m[0][1] * matrix . m[1][2] - matrix . m[0][2] * matrix . m[1][1]) / determinant;
  out -> m[1][0] = (matrix . m[1][2] * matrix . m[2][0] - matrix . m[1][0] * matrix . m[2][2]) / determinant;
  out -> m[1][1] = (matrix . m[0][0] * matrix . m[2][2] - matrix . m[0][2] * matrix . m[2][0]) / determinant;
  out -> m[1][2] = (matrix . m[0][2] * matrix . m[1][0] - matrix . m[0][0] * matrix . m[1][2]) / determinant;
  out -> m[2][0] = (matrix . m[1][0] * matrix . m[2][1] - matrix . m[1][1] * matrix . m[2][0]) / determinant;
  out -> m[2][1] = (matrix . m[0][1] * matrix . m[2][0] - matrix . m[0][0] * matrix . m[2][1]) / determinant;
  out -> m[2][2] = (matrix . m[0][0] * matrix . m[1][1] - matrix . m[0][1] * matrix . m[1][0]) / determinant;
  return SIM_OK;
}
/** @brief 检查矩阵九个元素是否均为有限浮点数。 */

int matrix3_isfinite(Matrix3 matrix)
{
  int row;
  int column;
  for (row = 1; row <= (((unsigned long )3u) + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; row += 1) {
    for (column = 1; column <= (((unsigned long )3u) + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; column += 1) {
      if (!__builtin_isfinite(matrix . m[1 * row + -1LL][1 * column + -1LL])) {
        return 0;
      }
    }
  }
  return 1;
}
