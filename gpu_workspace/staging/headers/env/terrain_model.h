/** @file terrain_model.h
 *  @brief 地形高程、AGL、碰撞和视线遮挡查询。
 */
#ifndef ENV_TERRAIN_MODEL_H
#define ENV_TERRAIN_MODEL_H

#include "common/status.h"
#include "common/vec3.h"
#include "env/earth_model.h"
#include "env/geo_coordinate.h"
#include "env/map_tile.h"

#include <stddef.h>
#include <stdint.h>

/** @brief 单实例地形瓦片缓存槽位上限。 */
#define TERRAIN_TILE_CACHE_MAX_TILES 16u
/** @brief 单条地形瓦片路径最大长度。 */
#define TERRAIN_TILE_CACHE_PATH_SIZE 512u
/** @brief 运行时二进制地形索引魔数，ASCII "TIX1"。 */
#define TERRAIN_TILE_INDEX_MAGIC UINT32_C(0x54495831)
/** @brief 当前二进制地形索引格式版本。 */
#define TERRAIN_TILE_INDEX_VERSION 1u

/** @brief 地形瓦片缺失时的处理策略。 */
typedef enum TerrainMissingPolicy {
    MAP_MISSING_ERROR = 0,
    MAP_MISSING_FLAT_FILL = 1,
    MAP_MISSING_NEAREST = 2
} TerrainMissingPolicy;

/** @brief 一个可懒加载的地形瓦片索引项。 */
typedef struct TerrainTileIndexEntry {
    /** @brief 内部瓦片文件路径。 */
    char path[TERRAIN_TILE_CACHE_PATH_SIZE];
    /** @brief 覆盖区域最小纬度，单位 rad。 */
    double lat_min;
    /** @brief 覆盖区域最大纬度，单位 rad。 */
    double lat_max;
    /** @brief 覆盖区域最小经度，单位 rad。 */
    double lon_min;
    /** @brief 覆盖区域最大经度，单位 rad。 */
    double lon_max;
    /** @brief 非零表示边界字段有效，可用于加载前空间筛选。 */
    int has_bounds;
} TerrainTileIndexEntry;

/** @brief 固定槽位 LRU 地形瓦片缓存。 */
typedef struct TerrainTileCache {
    /** @brief 已知瓦片路径列表，通常来自 map.tile_path/map.tile_paths[]/map.tile_index_path。 */
    char paths[TERRAIN_TILE_CACHE_MAX_TILES][TERRAIN_TILE_CACHE_PATH_SIZE];
    /** @brief 路径数量。 */
    size_t path_count;
    /** @brief 可选瓦片边界，用于加载前空间筛选。 */
    double lat_min[TERRAIN_TILE_CACHE_MAX_TILES];
    double lat_max[TERRAIN_TILE_CACHE_MAX_TILES];
    double lon_min[TERRAIN_TILE_CACHE_MAX_TILES];
    double lon_max[TERRAIN_TILE_CACHE_MAX_TILES];
    /** @brief 边界有效标志。 */
    int bounds_valid[TERRAIN_TILE_CACHE_MAX_TILES];
    /** @brief 可同时驻留内存的瓦片数量。 */
    size_t capacity;
    /** @brief 缓存槽位。 */
    TerrainTile tiles[TERRAIN_TILE_CACHE_MAX_TILES];
    /** @brief 槽位对应的路径索引；无效槽为 SIZE_MAX。 */
    size_t path_indices[TERRAIN_TILE_CACHE_MAX_TILES];
    /** @brief LRU 使用序号。 */
    uint64_t last_used[TERRAIN_TILE_CACHE_MAX_TILES];
    /** @brief 当前已加载槽位数量。 */
    size_t loaded_count;
    /** @brief 查询递增计数器。 */
    uint64_t use_counter;
    /** @brief 累计从磁盘加载次数。 */
    uint64_t load_count;
    /** @brief 累计淘汰次数。 */
    uint64_t eviction_count;
} TerrainTileCache;

/** @brief 地形模型运行状态。 */
typedef struct TerrainModel {
    /** @brief 是否启用瓦片查询；关闭时返回平坦填充值。 */
    int enabled;
    /** @brief 只读瓦片数组，所有权属于调用方。 */
    const TerrainTile *tiles;
    /** @brief 瓦片数组元素数量。 */
    size_t tile_count;
    /** @brief 可选懒加载瓦片缓存；非空时优先于静态 tiles 数组。 */
    TerrainTileCache *cache;
    /** @brief 查询点不在任何瓦片内时的处理策略。 */
    TerrainMissingPolicy missing_policy;
    /** @brief 平坦填充策略使用的高程，单位米。 */
    double flat_fill_height_m;
    /** @brief 查询过程累计的非致命告警位。 */
    uint32_t warning_flags;
} TerrainModel;

/** @brief 初始化不拥有瓦片内存的地形查询模型。 */
SimStatus terrain_model_init(
    TerrainModel *terrain,
    const TerrainTile *tiles,
    size_t tile_count,
    TerrainMissingPolicy missing_policy,
    double flat_fill_height_m);
/** @brief 初始化固定容量地形瓦片缓存。 */
SimStatus terrain_tile_cache_init(
    TerrainTileCache *cache,
    char paths[][TERRAIN_TILE_CACHE_PATH_SIZE],
    size_t path_count,
    size_t capacity);
/** @brief 使用带边界的索引项初始化固定容量地形瓦片缓存。 */
SimStatus terrain_tile_cache_init_from_index(
    TerrainTileCache *cache,
    const TerrainTileIndexEntry *entries,
    size_t entry_count,
    size_t capacity);
/** @brief 释放缓存中已加载瓦片。 */
void terrain_tile_cache_unload(TerrainTileCache *cache);
/** @brief 初始化带懒加载缓存的地形查询模型。 */
SimStatus terrain_model_init_with_cache(
    TerrainModel *terrain,
    TerrainTileCache *cache,
    TerrainMissingPolicy missing_policy,
    double flat_fill_height_m);
/** @brief 查询指定经纬度的地形高程。 */
SimStatus terrain_get_height(
    TerrainModel *terrain,
    double lat_rad,
    double lon_rad,
    double *height_m);
/** @brief 计算大地高度相对地形的 AGL。 */
SimStatus terrain_get_agl(
    TerrainModel *terrain,
    const LlaCoord *position,
    double *agl_m);
/** @brief 判断位置是否接触或进入地表。 */
SimStatus terrain_is_surface_collision(
    TerrainModel *terrain,
    const LlaCoord *position,
    int *collision);
/** @brief 沿 ECEF 线段采样判断地形遮挡。 */
SimStatus terrain_line_of_sight_occluded(
    TerrainModel *terrain,
    const EarthModel *earth,
    Vec3 start_ecef_m,
    Vec3 end_ecef_m,
    size_t interior_sample_count,
    int *occluded);

#endif
