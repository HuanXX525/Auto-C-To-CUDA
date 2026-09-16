#include <cstdio>
#include <cstdlib>

#include "host_environment_packer.hpp"

extern "C" {
#include "env/aero_database.h"
#include "env/map_tile.h"
}

int main(int argc, char **argv) {
    if (argc != 3) return EXIT_FAILURE;
    const char *aero_path = argv[1];
    const char *terrain_path = argv[2];
    const AeroTableSample samples[] = {
        {0.0, 0.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0},
        {1.0, 0.1, 0.1, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0},
    };
    if (aero_database_write_file(
            aero_path, samples, 2u, AERO_DB_EXTRAPOLATION_CLAMP_AND_WARN) != SIM_OK) {
        return EXIT_FAILURE;
    }
    TerrainTileHeader header{};
    header.magic = TERRAIN_TILE_MAGIC;
    header.version = TERRAIN_TILE_VERSION;
    header.grid_width = 2u;
    header.grid_height = 2u;
    header.lat_min = 0.0;
    header.lat_max = 1.0;
    header.lon_min = 0.0;
    header.lon_max = 1.0;
    header.height_scale = 1.0;
    header.height_offset = 0.0;
    int16_t terrain_samples[] = {0, 10, 20, 30};
    TerrainTile tile{};
    if (map_tile_bind(&tile, &header, terrain_samples, 4u) != SIM_OK ||
        map_tile_write_file(terrain_path, &tile) != SIM_OK) {
        return EXIT_FAILURE;
    }

    gpu_campaign::DeviceAeroTable aero{};
    gpu_campaign::DeviceTerrainGrid terrain{};
    AeroTableSample selected{};
    AeroTableSample host_selected{};
    AeroDatabase host_database{};
    std::uint32_t flags = 0;
    std::uint32_t host_flags = 0;
    double height = 0.0;
    double host_height = 0.0;
    TerrainTile loaded_tile{};
    if (aero_database_load_file(aero_path, &host_database) != SIM_OK ||
        aero_database_lookup(&host_database, 0.9, 0.09, 0.09,
                             &host_selected, &host_flags) != SIM_OK ||
        gpu_campaign::pack_aero_database_file(aero_path, &aero) != SIM_OK ||
        gpu_campaign::device_aero_table_lookup(
            &aero, 0.9, 0.09, 0.09, &selected, &flags) != SIM_OK ||
        flags != host_flags || selected.cx != host_selected.cx ||
        selected.cy != host_selected.cy || selected.cz != host_selected.cz ||
        selected.cl != host_selected.cl || selected.cm != host_selected.cm ||
        selected.cn != host_selected.cn ||
        gpu_campaign::pack_terrain_tile_file(
            terrain_path, &terrain,
            gpu_campaign::DeviceExtrapolationPolicy::ClampAndWarn) != SIM_OK ||
        map_tile_load_file(terrain_path, &loaded_tile) != SIM_OK ||
        map_tile_get_height(&loaded_tile, 0.5, 0.5, &host_height) != SIM_OK ||
        gpu_campaign::device_terrain_grid_height(
            &terrain, 0.5, 0.5, &height, &flags) != SIM_OK ||
        height != host_height || flags != 0u) {
        aero_database_unload(&host_database);
        map_tile_unload(&loaded_tile);
        return EXIT_FAILURE;
    }
    aero_database_unload(&host_database);
    map_tile_unload(&loaded_tile);
    std::printf("host_environment_packer_probe: PASS aero_cx=%.1f terrain_height=%.1f\n",
                selected.cx, height);
    return EXIT_SUCCESS;
}
