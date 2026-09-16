/* Flat terrain tile generator for the terrain-dynamics verification
   (planA Phase 4 item 4). Writes a 2x2 flat tile covering the WGS-84 scene
   box around LLA(30 deg, 120 deg): the same area the campaign scene and the
   golden chain use. Bounds are radians (map_tile/unit-agnostic; the kernel
   queries with plant lat_rad/lon_rad).
*/
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "common/math_constants.h"

extern "C" {
#include "env/map_tile.h"
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr,
                     "usage: terrain_tile_generator <output.tile> "
                     "<flat_height_m>\n");
        return EXIT_FAILURE;
    }
    const double height = std::atof(argv[2]);
    TerrainTileHeader header{};
    header.magic = TERRAIN_TILE_MAGIC;
    header.version = TERRAIN_TILE_VERSION;
    header.grid_width = 2u;
    header.grid_height = 2u;
    /* +/-0.01 deg (~2.2 km) around the scene origin. */
    header.lat_min = (30.0 - 0.01) * SIM_DEG_TO_RAD;
    header.lat_max = (30.0 + 0.01) * SIM_DEG_TO_RAD;
    header.lon_min = (120.0 - 0.01) * SIM_DEG_TO_RAD;
    header.lon_max = (120.0 + 0.01) * SIM_DEG_TO_RAD;
    header.height_scale = 1.0;
    header.height_offset = height;
    std::int16_t samples[] = {0, 0, 0, 0};
    TerrainTile tile{};
    if (map_tile_bind(&tile, &header, samples, 4u) != SIM_OK ||
        map_tile_write_file(argv[1], &tile) != SIM_OK) {
        std::fprintf(stderr, "terrain_tile_generator: write failed: %s\n",
                     argv[1]);
        return EXIT_FAILURE;
    }
    std::printf("terrain_tile_generator: wrote %s flat_height=%.3f\n",
                argv[1], height);
    return EXIT_SUCCESS;
}
