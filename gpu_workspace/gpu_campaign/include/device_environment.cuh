#ifndef GPU_CAMPAIGN_DEVICE_ENVIRONMENT_CUH
#define GPU_CAMPAIGN_DEVICE_ENVIRONMENT_CUH

#include <cstdint>
#include <type_traits>

#include "common/device_qualifiers.h"
#include "common/status.h"
#include "env/aero_database.h"

namespace gpu_campaign {

constexpr std::uint32_t kDeviceAeroMaxSamples = 256;
constexpr std::uint32_t kDeviceTerrainMaxCells = 4096;

enum class DeviceExtrapolationPolicy : std::uint32_t {
    Error = 0,
    ClampAndWarn = 1,
    HoldLastValid = 2,
};

constexpr std::uint32_t kDeviceTableFlagExtrapolated = 1u;

/* Fixed-capacity, pointer-free representation uploaded once per campaign. */
struct DeviceAeroTable {
    AeroTableSample samples[kDeviceAeroMaxSamples];
    std::uint32_t sample_count;
    std::uint32_t policy;
    double mach_min;
    double mach_max;
    double alpha_min;
    double alpha_max;
    double beta_min;
    double beta_max;
};

/* Regular height grid; row-major indexing is deterministic on host/device. */
struct DeviceTerrainGrid {
    double lat_min;
    double lat_max;
    double lon_min;
    double lon_max;
    std::uint32_t rows;
    std::uint32_t cols;
    std::uint32_t policy;
    std::uint32_t reserved;
    double heights[kDeviceTerrainMaxCells];
};

C2CUDA_HOST_DEVICE inline SimStatus device_aero_table_init(
    DeviceAeroTable *table, const AeroTableSample *samples,
    std::uint32_t sample_count, DeviceExtrapolationPolicy policy) {
    if (table == nullptr || samples == nullptr || sample_count == 0 ||
        sample_count > kDeviceAeroMaxSamples) {
        return SIM_ERR_INVALID_ARG;
    }
    table->sample_count = sample_count;
    table->policy = static_cast<std::uint32_t>(policy);
    table->mach_min = samples[0].mach;
    table->mach_max = samples[0].mach;
    table->alpha_min = samples[0].alpha_rad;
    table->alpha_max = samples[0].alpha_rad;
    table->beta_min = samples[0].beta_rad;
    table->beta_max = samples[0].beta_rad;
    for (std::uint32_t index = 0; index < sample_count; ++index) {
        const AeroTableSample sample = samples[index];
        if (!__builtin_isfinite(sample.mach) || !__builtin_isfinite(sample.alpha_rad) ||
            !__builtin_isfinite(sample.beta_rad) || !__builtin_isfinite(sample.cx) ||
            !__builtin_isfinite(sample.cy) || !__builtin_isfinite(sample.cz) ||
            !__builtin_isfinite(sample.cl) || !__builtin_isfinite(sample.cm) ||
            !__builtin_isfinite(sample.cn)) {
            return SIM_ERR_CONFIG;
        }
        table->samples[index] = sample;
        if (sample.mach < table->mach_min) table->mach_min = sample.mach;
        if (sample.mach > table->mach_max) table->mach_max = sample.mach;
        if (sample.alpha_rad < table->alpha_min) table->alpha_min = sample.alpha_rad;
        if (sample.alpha_rad > table->alpha_max) table->alpha_max = sample.alpha_rad;
        if (sample.beta_rad < table->beta_min) table->beta_min = sample.beta_rad;
        if (sample.beta_rad > table->beta_max) table->beta_max = sample.beta_rad;
    }
    return SIM_OK;
}

C2CUDA_HOST_DEVICE inline SimStatus device_aero_table_lookup(
    const DeviceAeroTable *table, double mach, double alpha_rad, double beta_rad,
    AeroTableSample *out, std::uint32_t *flags) {
    if (table == nullptr || out == nullptr || table->sample_count == 0 ||
        table->sample_count > kDeviceAeroMaxSamples || !__builtin_isfinite(mach) ||
        !__builtin_isfinite(alpha_rad) || !__builtin_isfinite(beta_rad)) {
        return SIM_ERR_INVALID_ARG;
    }
    if (flags != nullptr) *flags = 0;
    const bool outside = mach < table->mach_min || mach > table->mach_max ||
        alpha_rad < table->alpha_min || alpha_rad > table->alpha_max ||
        beta_rad < table->beta_min || beta_rad > table->beta_max;
    if (outside && table->policy == static_cast<std::uint32_t>(DeviceExtrapolationPolicy::Error)) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    /* HOLD_LAST_VALID requires per-instance lookup history and is handled by
       the production force adapter.  The stateless legacy lookup refuses to
       approximate it with clamp semantics. */
    if (outside && table->policy ==
                       static_cast<std::uint32_t>(DeviceExtrapolationPolicy::HoldLastValid)) {
        return SIM_ERR_CONFIG;
    }
    if (outside && flags != nullptr) *flags |= kDeviceTableFlagExtrapolated;
    if (mach < table->mach_min) mach = table->mach_min;
    if (mach > table->mach_max) mach = table->mach_max;
    if (alpha_rad < table->alpha_min) alpha_rad = table->alpha_min;
    if (alpha_rad > table->alpha_max) alpha_rad = table->alpha_max;
    if (beta_rad < table->beta_min) beta_rad = table->beta_min;
    if (beta_rad > table->beta_max) beta_rad = table->beta_max;
    const double exact_epsilon = 1.0e-18;
    double weight_sum = 0.0;
    out->mach = mach;
    out->alpha_rad = alpha_rad;
    out->beta_rad = beta_rad;
    out->cx = 0.0;
    out->cy = 0.0;
    out->cz = 0.0;
    out->cl = 0.0;
    out->cm = 0.0;
    out->cn = 0.0;
    for (std::uint32_t index = 0; index < table->sample_count; ++index) {
        const AeroTableSample sample = table->samples[index];
        const double dm = mach - sample.mach;
        const double da = alpha_rad - sample.alpha_rad;
        const double db = beta_rad - sample.beta_rad;
        const double distance = dm * dm + da * da + db * db;
        if (distance <= exact_epsilon) {
            *out = sample;
            return SIM_OK;
        }
        const double weight = 1.0 / distance;
        weight_sum += weight;
        out->cx += weight * sample.cx;
        out->cy += weight * sample.cy;
        out->cz += weight * sample.cz;
        out->cl += weight * sample.cl;
        out->cm += weight * sample.cm;
        out->cn += weight * sample.cn;
    }
    if (weight_sum <= 0.0 || !__builtin_isfinite(weight_sum)) {
        return SIM_ERR_NUMERIC;
    }
    out->cx /= weight_sum;
    out->cy /= weight_sum;
    out->cz /= weight_sum;
    out->cl /= weight_sum;
    out->cm /= weight_sum;
    out->cn /= weight_sum;
    return (__builtin_isfinite(out->cx) && __builtin_isfinite(out->cy) &&
            __builtin_isfinite(out->cz) && __builtin_isfinite(out->cl) &&
            __builtin_isfinite(out->cm) && __builtin_isfinite(out->cn))
        ? SIM_OK : SIM_ERR_NUMERIC;
}

C2CUDA_HOST_DEVICE inline SimStatus device_terrain_grid_init(
    DeviceTerrainGrid *grid, const double *heights, std::uint32_t rows,
    std::uint32_t cols, double lat_min, double lat_max, double lon_min,
    double lon_max, DeviceExtrapolationPolicy policy) {
    if (grid == nullptr || heights == nullptr || rows < 2 || cols < 2 ||
        static_cast<std::uint64_t>(rows) * cols > kDeviceTerrainMaxCells ||
        !__builtin_isfinite(lat_min) || !__builtin_isfinite(lat_max) ||
        !__builtin_isfinite(lon_min) || !__builtin_isfinite(lon_max) ||
        lat_min >= lat_max || lon_min >= lon_max) {
        return SIM_ERR_INVALID_ARG;
    }
    grid->lat_min = lat_min;
    grid->lat_max = lat_max;
    grid->lon_min = lon_min;
    grid->lon_max = lon_max;
    grid->rows = rows;
    grid->cols = cols;
    grid->policy = static_cast<std::uint32_t>(policy);
    grid->reserved = 0;
    for (std::uint32_t index = 0; index < rows * cols; ++index) {
        if (!__builtin_isfinite(heights[index])) return SIM_ERR_CONFIG;
        grid->heights[index] = heights[index];
    }
    return SIM_OK;
}

C2CUDA_HOST_DEVICE inline SimStatus device_terrain_grid_height(
    const DeviceTerrainGrid *grid, double lat, double lon, double *height,
    std::uint32_t *flags) {
    if (grid == nullptr || height == nullptr || grid->rows < 2 || grid->cols < 2 ||
        !__builtin_isfinite(lat) || !__builtin_isfinite(lon)) {
        return SIM_ERR_INVALID_ARG;
    }
    if (flags != nullptr) *flags = 0;
    bool outside = lat < grid->lat_min || lat > grid->lat_max ||
        lon < grid->lon_min || lon > grid->lon_max;
    if (outside && grid->policy == static_cast<std::uint32_t>(DeviceExtrapolationPolicy::Error)) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    if (outside && flags != nullptr) *flags |= kDeviceTableFlagExtrapolated;
    if (lat < grid->lat_min) lat = grid->lat_min;
    if (lat > grid->lat_max) lat = grid->lat_max;
    if (lon < grid->lon_min) lon = grid->lon_min;
    if (lon > grid->lon_max) lon = grid->lon_max;
    const double u = (lat - grid->lat_min) / (grid->lat_max - grid->lat_min) * (grid->rows - 1u);
    const double v = (lon - grid->lon_min) / (grid->lon_max - grid->lon_min) * (grid->cols - 1u);
    std::uint32_t row = static_cast<std::uint32_t>(u);
    std::uint32_t col = static_cast<std::uint32_t>(v);
    if (row >= grid->rows - 1u) row = grid->rows - 2u;
    if (col >= grid->cols - 1u) col = grid->cols - 2u;
    const double fr = u - row;
    const double fc = v - col;
    const std::uint32_t base = row * grid->cols + col;
    const double h00 = grid->heights[base];
    const double h01 = grid->heights[base + 1u];
    const double h10 = grid->heights[base + grid->cols];
    const double h11 = grid->heights[base + grid->cols + 1u];
    *height = (1.0 - fr) * ((1.0 - fc) * h00 + fc * h01) +
        fr * ((1.0 - fc) * h10 + fc * h11);
    return SIM_OK;
}

static_assert(sizeof(DeviceAeroTable) > sizeof(AeroTableSample),
              "DeviceAeroTable layout unexpectedly collapsed");
static_assert(std::is_trivially_copyable<DeviceAeroTable>::value,
              "DeviceAeroTable must be trivially copyable");
static_assert(std::is_standard_layout<DeviceAeroTable>::value,
              "DeviceAeroTable must use standard layout");
static_assert(sizeof(DeviceTerrainGrid) <=
                  sizeof(double) * (kDeviceTerrainMaxCells + 8u) + 16u,
              "DeviceTerrainGrid exceeds fixed storage budget");
static_assert(std::is_trivially_copyable<DeviceTerrainGrid>::value,
              "DeviceTerrainGrid must be trivially copyable");
static_assert(std::is_standard_layout<DeviceTerrainGrid>::value,
              "DeviceTerrainGrid must use standard layout");

}  // namespace gpu_campaign

#endif
