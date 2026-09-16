/** @file random.h
 *  @brief 可复现伪随机数发生器。
 *
 *  发生器状态完全包含在 @c SimRandom 中，不使用全局状态。相同种子和相同
 *  调用顺序保证生成相同序列，便于多实例 Monte Carlo 复现。
 */
#ifndef COMMON_RANDOM_H
#define COMMON_RANDOM_H

#include <stdint.h>
#include "common/device_qualifiers.h"

/** @brief 伪随机数发生器内部状态。 */
typedef struct SimRandom {
    uint64_t state;
} SimRandom;

/** @brief 使用确定性种子初始化发生器。 */
C2CUDA_HOST_DEVICE void sim_random_seed(SimRandom *random, uint64_t seed);
/** @brief 返回一个均匀分布的 64 位无符号整数。 */
C2CUDA_HOST_DEVICE uint64_t sim_random_next_u64(SimRandom *random);
/** @brief 返回 [0, 1) 区间均匀分布浮点数。 */
C2CUDA_HOST_DEVICE double sim_random_uniform01(SimRandom *random);
/** @brief 返回给定均值和标准差的高斯分布样本。 */
C2CUDA_HOST_DEVICE double sim_random_normal(SimRandom *random, double mean, double standard_deviation);

#endif
