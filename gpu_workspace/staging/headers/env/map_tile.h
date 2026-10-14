/** @file map_tile.h
 *  @brief 地形瓦片头部定义。
 *
 *  该结构描述球形地图或地形瓦片文件的元数据，用于解析地形网格、边界和
 *  完整性校验。
 */
#ifndef ENV_MAP_TILE_H
#define ENV_MAP_TILE_H

#include "common/status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 内部地形瓦片魔数。 */
#define TERRAIN_TILE_MAGIC UINT32_C(0x54494C45)
/** @brief 当前地形瓦片格式版本。 */
#define TERRAIN_TILE_VERSION 1u
/** @brief 地形瓦片头固定线格式长度。 */
#define TERRAIN_TILE_HEADER_WIRE_SIZE 62u

/** @brief 地形瓦片文件头。 */
typedef struct TerrainTileHeader {
    /** @brief 文件魔数。 */
    uint32_t magic;
    /** @brief 文件格式版本。 */
    uint16_t version;
    /** @brief 网格宽度。 */
    uint16_t grid_width;
    /** @brief 网格高度。 */
    uint16_t grid_height;
    /** @brief 覆盖区域最小纬度。 */
    double lat_min;
    /** @brief 覆盖区域最大纬度。 */
    double lat_max;
    /** @brief 覆盖区域最小经度。 */
    double lon_min;
    /** @brief 覆盖区域最大经度。 */
    double lon_max;
    /** @brief 高度缩放因子。 */
    double height_scale;
    /** @brief 高度偏移量。 */
    double height_offset;
    /** @brief 数据区 CRC32。 */
    uint32_t data_crc32;
} TerrainTileHeader;

/** @brief 已加载的地形瓦片。 */
typedef struct TerrainTile {
    /** @brief 经过校验的瓦片元数据。 */
    TerrainTileHeader header;
    /** @brief 按纬度行、经度列排列的有符号定点高程样本。 */
    int16_t *samples;
    /** @brief 样本总数，应等于宽度乘高度。 */
    size_t sample_count;
    /** @brief 非零表示 samples 由加载函数分配并需要释放。 */
    int owns_samples;
} TerrainTile;

/** @brief 使用调用方提供的高程网格绑定一个瓦片。 */
SimStatus map_tile_bind(
    TerrainTile *tile,
    const TerrainTileHeader *header,
    int16_t *samples,
    size_t sample_count);
/** @brief 从内部二进制格式加载瓦片。 */
SimStatus map_tile_load_file(const char *path, TerrainTile *out);
/** @brief 将瓦片写为内部固定小端二进制格式。 */
SimStatus map_tile_write_file(const char *path, const TerrainTile *tile);
/** @brief 释放瓦片加载阶段分配的内存。 */
void map_tile_unload(TerrainTile *tile);
/** @brief 判断经纬度是否落在瓦片边界内。 */
int map_tile_contains(const TerrainTile *tile, double lat_rad, double lon_rad);
/** @brief 对瓦片高程网格执行双线性插值。 */
SimStatus map_tile_get_height(
    const TerrainTile *tile,
    double lat_rad,
    double lon_rad,
    double *height_m);

#ifdef __cplusplus
}
#endif

#endif
