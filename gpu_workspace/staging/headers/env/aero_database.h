/** @file aero_database.h
 *  @brief 气动系数表查询接口。
 */
#ifndef ENV_AERO_DATABASE_H
#define ENV_AERO_DATABASE_H

#include "common/status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 内部气动表文件魔数。 */
#define AERO_DATABASE_MAGIC UINT32_C(0x4145524f)
/** @brief 当前气动表文件格式版本。 */
#define AERO_DATABASE_VERSION 1u
/** @brief 气动表文件头固定线格式长度。 */
#define AERO_DATABASE_HEADER_WIRE_SIZE 28u
/** @brief 气动表文件样本固定线格式长度。 */
#define AERO_DATABASE_SAMPLE_WIRE_SIZE 72u

/** @brief 气动表包络外查询策略。 */
typedef enum AeroDatabaseExtrapolationPolicy {
    AERO_DB_EXTRAPOLATION_ERROR = 0,
    AERO_DB_EXTRAPOLATION_CLAMP_AND_WARN = 1,
    AERO_DB_EXTRAPOLATION_HOLD_LAST_VALID = 2
} AeroDatabaseExtrapolationPolicy;

/** @brief 查询结果标志：发生了包络外外推或替代。 */
#define AERO_DB_FLAG_EXTRAPOLATED (1u << 0u)

/** @brief 气动表角度单位标识。 */
typedef enum AeroDatabaseAngleUnit {
    AERO_DB_ANGLE_RADIANS = 1
} AeroDatabaseAngleUnit;

/** @brief 气动表系数单位标识。 */
typedef enum AeroDatabaseCoefficientUnit {
    AERO_DB_COEFFICIENT_DIMENSIONLESS = 1
} AeroDatabaseCoefficientUnit;

/** @brief 单个气动表样本。 */
typedef struct AeroTableSample {
    double mach;
    double alpha_rad;
    double beta_rad;
    double cx;
    double cy;
    double cz;
    double cl;
    double cm;
    double cn;
} AeroTableSample;

/** @brief 只读气动数据库运行状态。 */
typedef struct AeroDatabase {
    const AeroTableSample *samples;
    size_t sample_count;
    AeroDatabaseExtrapolationPolicy extrapolation_policy;
    AeroTableSample *owned_samples;
    AeroTableSample last_valid;
    int have_last_valid;
} AeroDatabase;

/** @brief 绑定气动表样本并校验基本数值有效性。 */
SimStatus aero_database_init(
    AeroDatabase *database,
    const AeroTableSample *samples,
    size_t sample_count,
    AeroDatabaseExtrapolationPolicy extrapolation_policy);

/** @brief 从内部固定小端二进制格式加载气动表，数据库拥有样本内存。 */
SimStatus aero_database_load_file(const char *path, AeroDatabase *database);

/** @brief 将气动表样本写为内部固定小端二进制格式。 */
SimStatus aero_database_write_file(
    const char *path,
    const AeroTableSample *samples,
    size_t sample_count,
    AeroDatabaseExtrapolationPolicy extrapolation_policy);

/** @brief 释放由 aero_database_load_file() 分配的样本内存。 */
void aero_database_unload(AeroDatabase *database);

/** @brief 查询给定 Mach/迎角/侧滑角下的气动系数。 */
SimStatus aero_database_lookup(
    AeroDatabase *database,
    double mach,
    double alpha_rad,
    double beta_rad,
    AeroTableSample *out,
    uint32_t *flags);

#ifdef __cplusplus
}
#endif

#endif
