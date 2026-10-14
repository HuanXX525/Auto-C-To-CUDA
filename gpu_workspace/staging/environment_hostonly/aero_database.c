/** @file aero_database.c
 *  @brief 气动系数表查询实现。
 */
#include "env/aero_database.h"

#include "common/crc32.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct AeroDatabaseReader {
    const unsigned char *data;
    size_t size;
    size_t offset;
} AeroDatabaseReader;

/** @brief 将 16 位整数按小端写入文件。 */
static SimStatus write_u16(FILE *file, uint16_t value)
{
    unsigned char bytes[2];

    bytes[0] = (unsigned char)(value & UINT16_C(0x00ff));
    bytes[1] = (unsigned char)((value >> 8u) & UINT16_C(0x00ff));
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes) ? SIM_OK : SIM_ERR_IO;
}

/** @brief 将 32 位整数按小端写入文件。 */
static SimStatus write_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4];
    size_t index;

    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (unsigned char)((value >> (8u * index)) & UINT32_C(0xff));
    }
    return fwrite(bytes, 1u, sizeof(bytes), file) == sizeof(bytes) ? SIM_OK : SIM_ERR_IO;
}

/** @brief 将双精度原始位模式按小端写入缓冲区。 */
static void encode_double(unsigned char *bytes, double value)
{
    uint64_t bits;
    size_t index;

    (void)memcpy(&bits, &value, sizeof(bits));
    for (index = 0u; index < 8u; ++index) {
        bytes[index] = (unsigned char)((bits >> (8u * index)) & UINT64_C(0xff));
    }
}

/** @brief 从小端字节流读取 16 位整数。 */
static SimStatus read_u16(AeroDatabaseReader *reader, uint16_t *out)
{
    if (reader == 0 || out == 0 || reader->offset + 2u > reader->size) {
        return SIM_ERR_CONFIG;
    }
    *out = (uint16_t)reader->data[reader->offset] |
        (uint16_t)((uint16_t)reader->data[reader->offset + 1u] << 8u);
    reader->offset += 2u;
    return SIM_OK;
}

/** @brief 从小端字节流读取 32 位整数。 */
static SimStatus read_u32(AeroDatabaseReader *reader, uint32_t *out)
{
    size_t index;
    uint32_t value = 0u;

    if (reader == 0 || out == 0 || reader->offset + 4u > reader->size) {
        return SIM_ERR_CONFIG;
    }
    for (index = 0u; index < 4u; ++index) {
        value |= (uint32_t)reader->data[reader->offset + index] << (8u * index);
    }
    reader->offset += 4u;
    *out = value;
    return SIM_OK;
}

/** @brief 从小端字节流读取双精度原始位模式。 */
static SimStatus read_double(AeroDatabaseReader *reader, double *out)
{
    uint64_t bits = 0u;
    size_t index;

    if (reader == 0 || out == 0 || reader->offset + 8u > reader->size) {
        return SIM_ERR_CONFIG;
    }
    for (index = 0u; index < 8u; ++index) {
        bits |= (uint64_t)reader->data[reader->offset + index] << (8u * index);
    }
    reader->offset += 8u;
    (void)memcpy(out, &bits, sizeof(bits));
    return SIM_OK;
}

/** @brief 校验单个表点是否为有限数值。 */
static int sample_isfinite(const AeroTableSample *sample)
{
    return sample != 0 &&
        isfinite(sample->mach) &&
        isfinite(sample->alpha_rad) &&
        isfinite(sample->beta_rad) &&
        isfinite(sample->cx) &&
        isfinite(sample->cy) &&
        isfinite(sample->cz) &&
        isfinite(sample->cl) &&
        isfinite(sample->cm) &&
        isfinite(sample->cn);
}

/** @brief 比较两个样本是否严格按 Mach/AoA/beta 字典序递增。 */
static int sample_key_less(const AeroTableSample *left, const AeroTableSample *right)
{
    if (left->mach != right->mach) {
        return left->mach < right->mach;
    }
    if (left->alpha_rad != right->alpha_rad) {
        return left->alpha_rad < right->alpha_rad;
    }
    return left->beta_rad < right->beta_rad;
}

/** @brief 校验样本有效且按网格键单调排列。 */
static SimStatus validate_samples(const AeroTableSample *samples, size_t sample_count)
{
    size_t index;

    if (samples == 0 || sample_count == 0u) {
        return SIM_ERR_INVALID_ARG;
    }
    for (index = 0u; index < sample_count; ++index) {
        if (!sample_isfinite(&samples[index]) || samples[index].mach < 0.0) {
            return SIM_ERR_CONFIG;
        }
        if (index > 0u && !sample_key_less(&samples[index - 1u], &samples[index])) {
            return SIM_ERR_CONFIG;
        }
    }
    return SIM_OK;
}

/** @brief 将样本编码为固定小端线格式。 */
static void encode_sample(const AeroTableSample *sample, unsigned char *bytes)
{
    encode_double(bytes + 0u, sample->mach);
    encode_double(bytes + 8u, sample->alpha_rad);
    encode_double(bytes + 16u, sample->beta_rad);
    encode_double(bytes + 24u, sample->cx);
    encode_double(bytes + 32u, sample->cy);
    encode_double(bytes + 40u, sample->cz);
    encode_double(bytes + 48u, sample->cl);
    encode_double(bytes + 56u, sample->cm);
    encode_double(bytes + 64u, sample->cn);
}

/** @brief 从固定小端线格式解析样本。 */
static SimStatus decode_sample(AeroDatabaseReader *reader, AeroTableSample *sample)
{
    SimStatus status;

    status = read_double(reader, &sample->mach);
    if (status == SIM_OK) {
        status = read_double(reader, &sample->alpha_rad);
    }
    if (status == SIM_OK) {
        status = read_double(reader, &sample->beta_rad);
    }
    if (status == SIM_OK) {
        status = read_double(reader, &sample->cx);
    }
    if (status == SIM_OK) {
        status = read_double(reader, &sample->cy);
    }
    if (status == SIM_OK) {
        status = read_double(reader, &sample->cz);
    }
    if (status == SIM_OK) {
        status = read_double(reader, &sample->cl);
    }
    if (status == SIM_OK) {
        status = read_double(reader, &sample->cm);
    }
    if (status == SIM_OK) {
        status = read_double(reader, &sample->cn);
    }
    return status;
}

/** @brief 累积样本包络范围。 */
static SimStatus compute_envelope(
    const AeroDatabase *database,
    double *mach_min,
    double *mach_max,
    double *alpha_min,
    double *alpha_max,
    double *beta_min,
    double *beta_max)
{
    size_t index;

    if (database == 0 || database->samples == 0 || database->sample_count == 0u ||
        mach_min == 0 || mach_max == 0 ||
        alpha_min == 0 || alpha_max == 0 ||
        beta_min == 0 || beta_max == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    *mach_min = DBL_MAX;
    *alpha_min = DBL_MAX;
    *beta_min = DBL_MAX;
    *mach_max = -DBL_MAX;
    *alpha_max = -DBL_MAX;
    *beta_max = -DBL_MAX;
    for (index = 0u; index < database->sample_count; ++index) {
        const AeroTableSample *sample = &database->samples[index];
        if (!sample_isfinite(sample)) {
            return SIM_ERR_INVALID_ARG;
        }
        if (sample->mach < *mach_min) {
            *mach_min = sample->mach;
        }
        if (sample->mach > *mach_max) {
            *mach_max = sample->mach;
        }
        if (sample->alpha_rad < *alpha_min) {
            *alpha_min = sample->alpha_rad;
        }
        if (sample->alpha_rad > *alpha_max) {
            *alpha_max = sample->alpha_rad;
        }
        if (sample->beta_rad < *beta_min) {
            *beta_min = sample->beta_rad;
        }
        if (sample->beta_rad > *beta_max) {
            *beta_max = sample->beta_rad;
        }
    }
    return SIM_OK;
}

/** @brief 将值限制到闭区间。 */
static double clamp_double(double value, double minimum, double maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

/** @brief 对表格点执行反距离加权插值。 */
static SimStatus interpolate_inverse_distance(
    const AeroDatabase *database,
    double mach,
    double alpha_rad,
    double beta_rad,
    AeroTableSample *out)
{
    const double exact_epsilon = 1.0e-18;
    double weight_sum = 0.0;
    size_t index;

    if (database == 0 || out == 0 || database->samples == 0 ||
        database->sample_count == 0u) {
        return SIM_ERR_INVALID_ARG;
    }
    (void)memset(out, 0, sizeof(*out));
    out->mach = mach;
    out->alpha_rad = alpha_rad;
    out->beta_rad = beta_rad;

    for (index = 0u; index < database->sample_count; ++index) {
        const AeroTableSample *sample = &database->samples[index];
        double dm = mach - sample->mach;
        double da = alpha_rad - sample->alpha_rad;
        double db = beta_rad - sample->beta_rad;
        double distance_squared = dm * dm + da * da + db * db;
        double weight;

        if (distance_squared <= exact_epsilon) {
            *out = *sample;
            return SIM_OK;
        }
        weight = 1.0 / distance_squared;
        weight_sum += weight;
        out->cx += weight * sample->cx;
        out->cy += weight * sample->cy;
        out->cz += weight * sample->cz;
        out->cl += weight * sample->cl;
        out->cm += weight * sample->cm;
        out->cn += weight * sample->cn;
    }
    if (weight_sum <= 0.0 || !isfinite(weight_sum)) {
        return SIM_ERR_NUMERIC;
    }
    out->cx /= weight_sum;
    out->cy /= weight_sum;
    out->cz /= weight_sum;
    out->cl /= weight_sum;
    out->cm /= weight_sum;
    out->cn /= weight_sum;
    return sample_isfinite(out) ? SIM_OK : SIM_ERR_NUMERIC;
}

SimStatus aero_database_init(
    AeroDatabase *database,
    const AeroTableSample *samples,
    size_t sample_count,
    AeroDatabaseExtrapolationPolicy extrapolation_policy)
{
    if (database == 0 || samples == 0 || sample_count == 0u) {
        return SIM_ERR_INVALID_ARG;
    }
    if (extrapolation_policy != AERO_DB_EXTRAPOLATION_ERROR &&
        extrapolation_policy != AERO_DB_EXTRAPOLATION_CLAMP_AND_WARN &&
        extrapolation_policy != AERO_DB_EXTRAPOLATION_HOLD_LAST_VALID) {
        return SIM_ERR_INVALID_ARG;
    }
    if (validate_samples(samples, sample_count) != SIM_OK) {
        return SIM_ERR_CONFIG;
    }
    (void)memset(database, 0, sizeof(*database));
    database->samples = samples;
    database->sample_count = sample_count;
    database->extrapolation_policy = extrapolation_policy;
    return SIM_OK;
}

SimStatus aero_database_write_file(
    const char *path,
    const AeroTableSample *samples,
    size_t sample_count,
    AeroDatabaseExtrapolationPolicy extrapolation_policy)
{
    FILE *file;
    unsigned char *sample_bytes;
    size_t data_size;
    size_t index;
    uint32_t crc;
    SimStatus status;

    if (path == 0 || samples == 0 || sample_count == 0u) {
        return SIM_ERR_INVALID_ARG;
    }
    if (sample_count > UINT32_MAX ||
        sample_count > SIZE_MAX / AERO_DATABASE_SAMPLE_WIRE_SIZE) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    status = validate_samples(samples, sample_count);
    if (status != SIM_OK) {
        return status;
    }
    if (extrapolation_policy != AERO_DB_EXTRAPOLATION_ERROR &&
        extrapolation_policy != AERO_DB_EXTRAPOLATION_CLAMP_AND_WARN &&
        extrapolation_policy != AERO_DB_EXTRAPOLATION_HOLD_LAST_VALID) {
        return SIM_ERR_INVALID_ARG;
    }
    data_size = sample_count * AERO_DATABASE_SAMPLE_WIRE_SIZE;
    sample_bytes = (unsigned char *)malloc(data_size);
    if (sample_bytes == 0) {
        return SIM_ERR_INTERNAL;
    }
    for (index = 0u; index < sample_count; ++index) {
        encode_sample(&samples[index], sample_bytes + (index * AERO_DATABASE_SAMPLE_WIRE_SIZE));
    }
    crc = crc32_compute(sample_bytes, data_size);
    file = fopen(path, "wb");
    if (file == 0) {
        free(sample_bytes);
        return SIM_ERR_IO;
    }
    status = write_u32(file, AERO_DATABASE_MAGIC);
    if (status == SIM_OK) {
        status = write_u16(file, AERO_DATABASE_VERSION);
    }
    if (status == SIM_OK) {
        status = write_u16(file, AERO_DATABASE_HEADER_WIRE_SIZE);
    }
    if (status == SIM_OK) {
        status = write_u32(file, (uint32_t)sample_count);
    }
    if (status == SIM_OK) {
        status = write_u32(file, (uint32_t)extrapolation_policy);
    }
    if (status == SIM_OK) {
        status = write_u32(file, (uint32_t)AERO_DB_ANGLE_RADIANS);
    }
    if (status == SIM_OK) {
        status = write_u32(file, (uint32_t)AERO_DB_COEFFICIENT_DIMENSIONLESS);
    }
    if (status == SIM_OK) {
        status = write_u32(file, crc);
    }
    if (status == SIM_OK && fwrite(sample_bytes, 1u, data_size, file) != data_size) {
        status = SIM_ERR_IO;
    }
    free(sample_bytes);
    if (fclose(file) != 0 && status == SIM_OK) {
        status = SIM_ERR_IO;
    }
    return status;
}

SimStatus aero_database_load_file(const char *path, AeroDatabase *database)
{
    FILE *file;
    long file_size;
    unsigned char *bytes;
    AeroTableSample *samples;
    AeroDatabaseReader reader;
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t sample_count_u32;
    uint32_t policy_u32;
    uint32_t angle_unit;
    uint32_t coefficient_unit;
    uint32_t data_crc32;
    size_t sample_count;
    size_t data_size;
    size_t index;
    SimStatus status;

    if (path == 0 || database == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    (void)memset(database, 0, sizeof(*database));
    file = fopen(path, "rb");
    if (file == 0) {
        return SIM_ERR_IO;
    }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (file_size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return SIM_ERR_IO;
    }
    if ((size_t)file_size < AERO_DATABASE_HEADER_WIRE_SIZE) {
        (void)fclose(file);
        return SIM_ERR_CONFIG;
    }
    bytes = (unsigned char *)malloc((size_t)file_size);
    if (bytes == 0) {
        (void)fclose(file);
        return SIM_ERR_INTERNAL;
    }
    if (fread(bytes, 1u, (size_t)file_size, file) != (size_t)file_size) {
        free(bytes);
        (void)fclose(file);
        return SIM_ERR_IO;
    }
    (void)fclose(file);
    reader.data = bytes;
    reader.size = AERO_DATABASE_HEADER_WIRE_SIZE;
    reader.offset = 0u;
    status = read_u32(&reader, &magic);
    if (status == SIM_OK) {
        status = read_u16(&reader, &version);
    }
    if (status == SIM_OK) {
        status = read_u16(&reader, &header_size);
    }
    if (status == SIM_OK) {
        status = read_u32(&reader, &sample_count_u32);
    }
    if (status == SIM_OK) {
        status = read_u32(&reader, &policy_u32);
    }
    if (status == SIM_OK) {
        status = read_u32(&reader, &angle_unit);
    }
    if (status == SIM_OK) {
        status = read_u32(&reader, &coefficient_unit);
    }
    if (status == SIM_OK) {
        status = read_u32(&reader, &data_crc32);
    }
    if (status != SIM_OK ||
        magic != AERO_DATABASE_MAGIC ||
        version != AERO_DATABASE_VERSION ||
        header_size != AERO_DATABASE_HEADER_WIRE_SIZE ||
        sample_count_u32 == 0u ||
        angle_unit != (uint32_t)AERO_DB_ANGLE_RADIANS ||
        coefficient_unit != (uint32_t)AERO_DB_COEFFICIENT_DIMENSIONLESS ||
        (policy_u32 != (uint32_t)AERO_DB_EXTRAPOLATION_ERROR &&
            policy_u32 != (uint32_t)AERO_DB_EXTRAPOLATION_CLAMP_AND_WARN &&
            policy_u32 != (uint32_t)AERO_DB_EXTRAPOLATION_HOLD_LAST_VALID)) {
        free(bytes);
        return SIM_ERR_CONFIG;
    }
    sample_count = (size_t)sample_count_u32;
    if (sample_count > SIZE_MAX / AERO_DATABASE_SAMPLE_WIRE_SIZE) {
        free(bytes);
        return SIM_ERR_OUT_OF_RANGE;
    }
    data_size = sample_count * AERO_DATABASE_SAMPLE_WIRE_SIZE;
    if (AERO_DATABASE_HEADER_WIRE_SIZE + data_size != (size_t)file_size ||
        crc32_compute(bytes + AERO_DATABASE_HEADER_WIRE_SIZE, data_size) != data_crc32) {
        free(bytes);
        return SIM_ERR_CONFIG;
    }
    samples = (AeroTableSample *)calloc(sample_count, sizeof(*samples));
    if (samples == 0) {
        free(bytes);
        return SIM_ERR_INTERNAL;
    }
    reader.data = bytes + AERO_DATABASE_HEADER_WIRE_SIZE;
    reader.size = data_size;
    reader.offset = 0u;
    for (index = 0u; index < sample_count; ++index) {
        status = decode_sample(&reader, &samples[index]);
        if (status != SIM_OK) {
            free(samples);
            free(bytes);
            return status;
        }
    }
    free(bytes);
    status = validate_samples(samples, sample_count);
    if (status != SIM_OK) {
        free(samples);
        return status;
    }
    database->samples = samples;
    database->owned_samples = samples;
    database->sample_count = sample_count;
    database->extrapolation_policy = (AeroDatabaseExtrapolationPolicy)policy_u32;
    return SIM_OK;
}

void aero_database_unload(AeroDatabase *database)
{
    if (database != 0) {
        free(database->owned_samples);
        (void)memset(database, 0, sizeof(*database));
    }
}

SimStatus aero_database_lookup(
    AeroDatabase *database,
    double mach,
    double alpha_rad,
    double beta_rad,
    AeroTableSample *out,
    uint32_t *flags)
{
    double mach_min;
    double mach_max;
    double alpha_min;
    double alpha_max;
    double beta_min;
    double beta_max;
    int out_of_envelope;
    SimStatus status;

    if (database == 0 || out == 0 || flags == 0 ||
        !isfinite(mach) || !isfinite(alpha_rad) || !isfinite(beta_rad)) {
        return SIM_ERR_INVALID_ARG;
    }
    *flags = 0u;
    status = compute_envelope(
        database,
        &mach_min,
        &mach_max,
        &alpha_min,
        &alpha_max,
        &beta_min,
        &beta_max);
    if (status != SIM_OK) {
        return status;
    }
    out_of_envelope =
        mach < mach_min || mach > mach_max ||
        alpha_rad < alpha_min || alpha_rad > alpha_max ||
        beta_rad < beta_min || beta_rad > beta_max;
    if (out_of_envelope != 0) {
        *flags |= AERO_DB_FLAG_EXTRAPOLATED;
        if (database->extrapolation_policy == AERO_DB_EXTRAPOLATION_ERROR) {
            return SIM_ERR_OUT_OF_RANGE;
        }
        if (database->extrapolation_policy == AERO_DB_EXTRAPOLATION_HOLD_LAST_VALID) {
            if (database->have_last_valid == 0) {
                return SIM_ERR_OUT_OF_RANGE;
            }
            *out = database->last_valid;
            return SIM_OK;
        }
        mach = clamp_double(mach, mach_min, mach_max);
        alpha_rad = clamp_double(alpha_rad, alpha_min, alpha_max);
        beta_rad = clamp_double(beta_rad, beta_min, beta_max);
    }
    status = interpolate_inverse_distance(database, mach, alpha_rad, beta_rad, out);
    if (status == SIM_OK) {
        database->last_valid = *out;
        database->have_last_valid = 1;
    }
    return status;
}
