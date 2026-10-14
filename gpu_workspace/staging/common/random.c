/** @file random.c
 *  @brief 可复现伪随机数发生器实现。
 */
#include "common/random.h"
#include "common/math_constants.h"

#include <math.h>

/** @brief 初始化 xorshift64* 状态；零种子替换为固定非零常量。 */
void sim_random_seed(SimRandom *random, uint64_t seed)
{
    if (random != 0) {
        random->state = seed == 0u ? UINT64_C(0x9E3779B97F4A7C15) : seed;
    }
}

/** @brief 生成下一个 xorshift64* 无符号整数。 */
uint64_t sim_random_next_u64(SimRandom *random)
{
    uint64_t value;

    if (random == 0) {
        return 0u;
    }
    value = random->state;
    value ^= value >> 12u;
    value ^= value << 25u;
    value ^= value >> 27u;
    random->state = value;
    return value * UINT64_C(2685821657736338717);
}

/** @brief 使用高 53 位构造双精度 [0,1) 均匀样本。 */
double sim_random_uniform01(SimRandom *random)
{
    const uint64_t value = sim_random_next_u64(random) >> 11u;
    return (double)value * (1.0 / 9007199254740992.0);
}

/** @brief 使用 Box-Muller 变换生成高斯样本。 */
double sim_random_normal(SimRandom *random, double mean, double standard_deviation)
{
    double u1;
    double u2;
    double standard_normal;

    if (random == 0 || !isfinite(mean) || !isfinite(standard_deviation) ||
        standard_deviation < 0.0) {
        return NAN;
    }

    do {
        u1 = sim_random_uniform01(random);
    } while (u1 <= 0.0);
    u2 = sim_random_uniform01(random);
    standard_normal = sqrt(-2.0 * log(u1)) * cos(SIM_TWO_PI * u2);
    return mean + (standard_deviation * standard_normal);
}
