#include "host_environment_packer.hpp"

#include <vector>

extern "C" {
#include "env/aero_database.h"
#include "env/map_tile.h"
}

namespace gpu_campaign {

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

}  // namespace gpu_campaign
