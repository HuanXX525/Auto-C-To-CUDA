#include "host_environment_packer.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include "env/aero_database.h"
#include "env/map_tile.h"
}

namespace gpu_campaign {

namespace {

AeroSurrogateLinearTerm *surrogate_term(AeroSurrogateModel *model,
                                         const char *name) {
    if (std::strcmp(name, "cx") == 0) return &model->cx;
    if (std::strcmp(name, "cy") == 0) return &model->cy;
    if (std::strcmp(name, "cz") == 0) return &model->cz;
    if (std::strcmp(name, "cl") == 0) return &model->cl;
    if (std::strcmp(name, "cm") == 0) return &model->cm;
    if (std::strcmp(name, "cn") == 0) return &model->cn;
    return nullptr;
}

bool finite_term(const AeroSurrogateLinearTerm &term) {
    return std::isfinite(term.offset) && std::isfinite(term.mach) &&
        std::isfinite(term.alpha_rad) && std::isfinite(term.beta_rad);
}

}  // namespace

SimStatus pack_aero_database_file(const char *path, DeviceAeroTable *out) {
    if (path == nullptr || out == nullptr) return SIM_ERR_INVALID_ARG;
    AeroDatabase database{};
    const SimStatus load_status = aero_database_load_file(path, &database);
    if (load_status != SIM_OK) return load_status;
    SimStatus status = SIM_OK;
    if (database.sample_count > kDeviceAeroMaxSamples) {
        status = SIM_ERR_OUT_OF_RANGE;
    } else if (database.extrapolation_policy == AERO_DB_EXTRAPOLATION_HOLD_LAST_VALID) {
        /* Device state is immutable in this adapter; do not alter semantics. */
        status = SIM_ERR_CONFIG;
    } else {
        const DeviceExtrapolationPolicy policy =
            database.extrapolation_policy == AERO_DB_EXTRAPOLATION_CLAMP_AND_WARN
                ? DeviceExtrapolationPolicy::ClampAndWarn
                : DeviceExtrapolationPolicy::Error;
        status = device_aero_table_init(
            out, database.samples,
            static_cast<std::uint32_t>(database.sample_count), policy);
    }
    aero_database_unload(&database);
    return status;
}

SimStatus pack_terrain_tile_file(
    const char *path, DeviceTerrainGrid *out,
    DeviceExtrapolationPolicy policy) {
    if (path == nullptr || out == nullptr) return SIM_ERR_INVALID_ARG;
    TerrainTile tile{};
    const SimStatus load_status = map_tile_load_file(path, &tile);
    if (load_status != SIM_OK) return load_status;
    const std::size_t rows = tile.header.grid_height;
    const std::size_t cols = tile.header.grid_width;
    SimStatus status = SIM_OK;
    if (rows * cols > kDeviceTerrainMaxCells) {
        status = SIM_ERR_OUT_OF_RANGE;
    } else {
        std::vector<double> heights(tile.sample_count);
        for (std::size_t index = 0; index < tile.sample_count; ++index) {
            heights[index] = tile.header.height_offset +
                tile.header.height_scale * static_cast<double>(tile.samples[index]);
        }
        status = device_terrain_grid_init(
            out, heights.data(), static_cast<std::uint32_t>(rows),
            static_cast<std::uint32_t>(cols), tile.header.lat_min,
            tile.header.lat_max, tile.header.lon_min, tile.header.lon_max, policy);
    }
    map_tile_unload(&tile);
    return status;
}

SimStatus pack_aero_surrogate_file(const char *path, AeroSurrogateModel *out) {
    if (path == nullptr || out == nullptr) return SIM_ERR_INVALID_ARG;
    std::FILE *file = std::fopen(path, "rb");
    if (file == nullptr) return SIM_ERR_IO;
    char magic[64];
    if (std::fscanf(file, "%63s", magic) != 1 ||
        std::strcmp(magic, "MISSILE_AERO_SURROGATE_LINEAR_V1") != 0) {
        std::fclose(file);
        return SIM_ERR_CONFIG;
    }
    *out = AeroSurrogateModel{};
    std::uint32_t seen = 0u;
    for (std::uint32_t index = 0u; index < 6u; ++index) {
        char name[8];
        AeroSurrogateLinearTerm term{};
        if (std::fscanf(file, "%7s %lf %lf %lf %lf", name, &term.offset,
                        &term.mach, &term.alpha_rad, &term.beta_rad) != 5) {
            std::fclose(file);
            return SIM_ERR_CONFIG;
        }
        AeroSurrogateLinearTerm *target = surrogate_term(out, name);
        const std::uint32_t bit = std::strcmp(name, "cx") == 0 ? 0x01u :
            std::strcmp(name, "cy") == 0 ? 0x02u :
            std::strcmp(name, "cz") == 0 ? 0x04u :
            std::strcmp(name, "cl") == 0 ? 0x08u :
            std::strcmp(name, "cm") == 0 ? 0x10u : 0x20u;
        if (target == nullptr || !finite_term(term) || (seen & bit) != 0u) {
            std::fclose(file);
            return SIM_ERR_CONFIG;
        }
        *target = term;
        seen |= bit;
    }
    std::fclose(file);
    return seen == 0x3fu ? SIM_OK : SIM_ERR_CONFIG;
}

}  // namespace gpu_campaign
