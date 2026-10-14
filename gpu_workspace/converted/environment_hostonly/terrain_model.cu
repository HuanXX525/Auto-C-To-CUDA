/** @file terrain_model.c
 *  @brief 地形查询、碰撞和视线遮挡实现。
 */
#include "env/terrain_model.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define TERRAIN_WARNING_MISSING_TILE UINT32_C(0x00000001)
/** @brief 绑定只读瓦片集合并配置缺失瓦片策略。 */
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

SimStatus terrain_model_init(TerrainModel *terrain,const TerrainTile *tiles,size_t tile_count,TerrainMissingPolicy missing_policy,double flat_fill_height_m)
{
  if (terrain == 0 || tiles == 0 && tile_count != 0u) {
    return SIM_ERR_INVALID_ARG;
  }
  if (missing_policy < MAP_MISSING_ERROR || missing_policy > MAP_MISSING_NEAREST || !__builtin_isfinite(flat_fill_height_m)) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  terrain -> enabled = 1;
  terrain -> tiles = tiles;
  terrain -> tile_count = tile_count;
  terrain -> cache = 0;
  terrain -> missing_policy = missing_policy;
  terrain -> flat_fill_height_m = flat_fill_height_m;
  terrain -> warning_flags = 0u;
  return SIM_OK;
}

SimStatus terrain_tile_cache_init(TerrainTileCache *cache,char paths[][512],size_t path_count,size_t capacity)
{
  TerrainTileIndexEntry entries[16];
  size_t index;
  if (cache == 0 || paths == 0 && path_count != 0u) {
    return SIM_ERR_INVALID_ARG;
  }
  if (path_count > 16u || capacity == 0u || capacity > 16u) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  (void )(memset(entries,0,sizeof(entries)));
  for (index = 1; index <= (path_count + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; index += 1) {
    int written;
    if (paths[1 * index + -1LL][0] == '\0') {
      return SIM_ERR_CONFIG;
    }
    written = snprintf(entries[1 * index + -1LL] . path,sizeof(entries[1 * index + -1LL] . path),"%s",paths[1 * index + -1LL]);
    if (written < 0 || ((size_t )written) >= sizeof(entries[1 * index + -1LL] . path)) {
      return SIM_ERR_OUT_OF_RANGE;
    }
    entries[1 * index + -1LL] . has_bounds = 0;
  }
  return terrain_tile_cache_init_from_index(cache,entries,path_count,capacity);
}

SimStatus terrain_tile_cache_init_from_index(TerrainTileCache *cache,const TerrainTileIndexEntry *entries,size_t entry_count,size_t capacity)
{
  size_t index;
  if (cache == 0 || entries == 0 && entry_count != 0u) {
    return SIM_ERR_INVALID_ARG;
  }
  if (entry_count > 16u || capacity == 0u || capacity > 16u) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  (void )(memset(cache,0,sizeof(( *cache))));
  cache -> path_count = entry_count;
  cache -> capacity = capacity;
  for (index = 1; index <= (((unsigned long )16u) + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; index += 1) {
    cache -> path_indices[1 * index + -1LL] = ((size_t )-1);
  }
  for (index = 0u; index < entry_count; ++index) {
    int written;
    if (entries[index] . path[0] == '\0') {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;{
        TerrainTileCache *cache__17_inline_12 = cache;
        size_t index_inline_13;
        if (cache__17_inline_12 == 0) {{
            ;
            goto rose_inline_end__18;
          }
        }
        for (index_inline_13 = 0u; index_inline_13 < 16u; ++index_inline_13) {
          map_tile_unload(&cache__17_inline_12 -> tiles[index_inline_13]);
          cache__17_inline_12 -> path_indices[index_inline_13] = ((size_t )(- 1));
          cache__17_inline_12 -> last_used[index_inline_13] = 0u;
        }
        cache__17_inline_12 -> loaded_count = 0u;
        rose_inline_end__18:
        ;
      }
      return SIM_ERR_CONFIG;
    }
    if (entries[index] . has_bounds != 0 && (!__builtin_isfinite(entries[index] . lat_min) || !__builtin_isfinite(entries[index] . lat_max) || !__builtin_isfinite(entries[index] . lon_min) || !__builtin_isfinite(entries[index] . lon_max) || entries[index] . lat_min >= entries[index] . lat_max || entries[index] . lon_min >= entries[index] . lon_max)) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;{
        TerrainTileCache *cache__20_inline_14 = cache;
        size_t index_inline_15;
        if (cache__20_inline_14 == 0) {{
            ;
            goto rose_inline_end__21;
          }
        }
        for (index_inline_15 = 0u; index_inline_15 < 16u; ++index_inline_15) {
          map_tile_unload(&cache__20_inline_14 -> tiles[index_inline_15]);
          cache__20_inline_14 -> path_indices[index_inline_15] = ((size_t )(- 1));
          cache__20_inline_14 -> last_used[index_inline_15] = 0u;
        }
        cache__20_inline_14 -> loaded_count = 0u;
        rose_inline_end__21:
        ;
      }
      return SIM_ERR_CONFIG;
    }
    written = snprintf(cache -> paths[index],sizeof(cache -> paths[index]),"%s",entries[index] . path);
    if (written < 0 || ((size_t )written) >= sizeof(cache -> paths[index])) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;{
        TerrainTileCache *cache__23_inline_16 = cache;
        size_t index_inline_17;
        if (cache__23_inline_16 == 0) {{
            ;
            goto rose_inline_end__24;
          }
        }
        for (index_inline_17 = 0u; index_inline_17 < 16u; ++index_inline_17) {
          map_tile_unload(&cache__23_inline_16 -> tiles[index_inline_17]);
          cache__23_inline_16 -> path_indices[index_inline_17] = ((size_t )(- 1));
          cache__23_inline_16 -> last_used[index_inline_17] = 0u;
        }
        cache__23_inline_16 -> loaded_count = 0u;
        rose_inline_end__24:
        ;
      }
      return SIM_ERR_OUT_OF_RANGE;
    }
    cache -> bounds_valid[index] = (entries[index] . has_bounds != 0?1 : 0);
    cache -> lat_min[index] = entries[index] . lat_min;
    cache -> lat_max[index] = entries[index] . lat_max;
    cache -> lon_min[index] = entries[index] . lon_min;
    cache -> lon_max[index] = entries[index] . lon_max;
  }
  return SIM_OK;
}

void terrain_tile_cache_unload(TerrainTileCache *cache)
{
  size_t index;
  if (cache == 0) {
    return ;
  }
  for (index = 1; index <= (((unsigned long )16u) + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; index += 1) {
    map_tile_unload(&cache -> tiles[1 * index + -1LL]);
    cache -> path_indices[1 * index + -1LL] = ((size_t )-1);
    cache -> last_used[1 * index + -1LL] = 0u;
  }
  cache -> loaded_count = 0u;
}

SimStatus terrain_model_init_with_cache(TerrainModel *terrain,TerrainTileCache *cache,TerrainMissingPolicy missing_policy,double flat_fill_height_m)
{
  SimStatus status;
  if (cache == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  status = terrain_model_init(terrain,0,0u,missing_policy,flat_fill_height_m);
  if (status != SIM_OK) {
    return status;
  }
  terrain -> cache = cache;
  return SIM_OK;
}
/** @brief 将标量限制在闭区间内。 */

static double clamp_value(double value,double minimum,double maximum)
{
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}
/** @brief 查询缓存中路径索引对应的槽位。 */

static TerrainTile *cache_find_loaded_by_path(TerrainTileCache *cache,size_t path_index)
{
  size_t index;
  if (cache == 0) {
    return 0;
  }
  for (index = 1; index <= (cache -> capacity + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; index += 1) {
    if (cache -> path_indices[1 * index + -1LL] == path_index) {
      cache -> last_used[1 * index + -1LL] = ++cache -> use_counter;
      return &cache -> tiles[1 * index + -1LL];
    }
  }
  return 0;
}
/** @brief 根据索引边界判断查询点是否可能落入指定路径对应瓦片。 */

static int cache_entry_may_contain(const TerrainTileCache *cache,size_t path_index,double lat_rad,double lon_rad)
{
  if (cache == 0 || path_index >= cache -> path_count) {
    return 0;
  }
  if (cache -> bounds_valid[path_index] == 0) {
    return 1;
  }
  return lat_rad >= cache -> lat_min[path_index] && lat_rad <= cache -> lat_max[path_index] && lon_rad >= cache -> lon_min[path_index] && lon_rad <= cache -> lon_max[path_index];
}
/** @brief 为新瓦片选择空槽或 LRU 淘汰槽。 */

static SimStatus cache_select_slot(TerrainTileCache *cache,size_t *slot_out)
{
  size_t index;
  size_t selected = 0u;
  uint64_t selected_use = 18446744073709551615UL;
  if (cache == 0 || slot_out == 0 || cache -> capacity == 0u) {
    return SIM_ERR_INVALID_ARG;
  }
  for (index = 1; index <= (cache -> capacity + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; index += 1) {
    if (cache -> path_indices[1 * index + -1LL] == ((size_t )-1)) {
       *slot_out = 1 * index + -1LL;
      return SIM_OK;
    }
  }
  for (index = 1; index <= (cache -> capacity + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; index += 1) {
    if (cache -> last_used[1 * index + -1LL] < selected_use) {
      selected_use = cache -> last_used[1 * index + -1LL];
      selected = 1 * index + -1LL;
    }
  }
  map_tile_unload(&cache -> tiles[selected]);
  cache -> path_indices[selected] = ((size_t )(- 1));
  cache -> last_used[selected] = 0u;
  if (cache -> loaded_count > 0u) {
    --cache -> loaded_count;
  }
  ++cache -> eviction_count;
   *slot_out = selected;
  return SIM_OK;
}
/** @brief 加载指定路径索引到缓存并返回槽位。 */

static SimStatus cache_load_path(TerrainTileCache *cache,size_t path_index,TerrainTile **tile_out)
{
  TerrainTile *loaded;
  size_t slot;
  SimStatus status;
  if (cache == 0 || tile_out == 0 || path_index >= cache -> path_count) {
    return SIM_ERR_INVALID_ARG;
  }
  loaded = cache_find_loaded_by_path(cache,path_index);
  if (loaded != 0) {
     *tile_out = loaded;
    return SIM_OK;
  }
  status = cache_select_slot(cache,&slot);
  if (status != SIM_OK) {
    return status;
  }
  status = map_tile_load_file(cache -> paths[path_index],&cache -> tiles[slot]);
  if (status != SIM_OK) {
    return status;
  }
  cache -> path_indices[slot] = path_index;
  cache -> last_used[slot] = ++cache -> use_counter;
  ++cache -> loaded_count;
  ++cache -> load_count;
   *tile_out = &cache -> tiles[slot];
  return SIM_OK;
}
/** @brief 尝试更新最近瓦片候选高程。 */

static void update_nearest_candidate(const TerrainTile *tile,double lat_rad,double lon_rad,double *nearest_distance,double *nearest_height,int *nearest_found)
{
  const double sample_lat = clamp_value(lat_rad,tile -> header . lat_min,tile -> header . lat_max);
  const double sample_lon = clamp_value(lon_rad,tile -> header . lon_min,tile -> header . lon_max);
  const double dlat = lat_rad - sample_lat;
  const double dlon = lon_rad - sample_lon;
  const double distance = dlat * dlat + dlon * dlon;
  double height = 0.0;
  if (nearest_distance == 0 || nearest_height == 0 || nearest_found == 0) {
    return ;
  }
  if ((map_tile_get_height(tile,sample_lat,sample_lon,&height)) != SIM_OK) {
    return ;
  }
  if ( *nearest_found == 0 || distance <  *nearest_distance) {
     *nearest_found = 1;
     *nearest_distance = distance;
     *nearest_height = height;
  }
}
/** @brief 使用懒加载缓存查询地形高程。 */

static SimStatus terrain_get_height_from_cache(TerrainModel *terrain,double lat_rad,double lon_rad,double *height_m)
{
  TerrainTileCache *cache = terrain -> cache;
  double nearest_distance = (double )1.79769313486231570814527423731704357e+308L;
  double nearest_height = 0.0;
  int nearest_found = 0;
  size_t index;
  for (index = 1; index <= (cache -> capacity + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; index += 1) {
    TerrainTile *tile = &cache -> tiles[1 * index + -1LL];
    if (cache -> path_indices[1 * index + -1LL] == ((size_t )-1)) {
      continue; 
    }
    if (map_tile_contains(tile,lat_rad,lon_rad)) {
      cache -> last_used[1 * index + -1LL] = ++cache -> use_counter;
      return map_tile_get_height(tile,lat_rad,lon_rad,height_m);
    }
    if ((terrain -> missing_policy) == MAP_MISSING_NEAREST) {
      update_nearest_candidate(tile,lat_rad,lon_rad,&nearest_distance,&nearest_height,&nearest_found);
    }
  }
  for (index = 0u; index < cache -> path_count; ++index) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
    TerrainTile *rose_temp__41;
{
      TerrainTileCache *cache__38 = cache;
      size_t path_index__39 = index;
      size_t index;
      if (cache__38 == 0) {{
          rose_temp__41 = 0;
          goto rose_inline_end__40;
        }
      }
      for (index = 0u; index < cache__38 -> capacity; ++index) {
        if (cache__38 -> path_indices[index] == path_index__39) {
          cache__38 -> last_used[index] = ++cache__38 -> use_counter;
{
            rose_temp__41 = &cache__38 -> tiles[index];
            goto rose_inline_end__40;
          }
        }
      }
{
        rose_temp__41 = 0;
        goto rose_inline_end__40;
      }
      rose_inline_end__40:
      ;
    }
    TerrainTile *tile = rose_temp__41;
    SimStatus status;
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
{
      _Bool rose__temp2_inline_18 = 1;
      int rose_temp__49_inline_19;
      if (tile == ((TerrainTile *)0) && ((unsigned int )(terrain -> missing_policy)) != ((unsigned int )MAP_MISSING_NEAREST)) {
        int rose_temp__50_inline_20;
{
          const TerrainTileCache *cache__43_inline_21 = cache;
          size_t path_index__44_inline_22 = index;
          double lat_rad__45_inline_23 = lat_rad;
          double lon_rad__46_inline_24 = lon_rad;
          if (cache__43_inline_21 == 0 || path_index__44_inline_22 >= cache__43_inline_21 -> path_count) {{
              rose_temp__50_inline_20 = 0;
              goto rose_inline_end__47;
            }
          }
          if (cache__43_inline_21 -> bounds_valid[path_index__44_inline_22] == 0) {{
              rose_temp__50_inline_20 = 1;
              goto rose_inline_end__47;
            }
          }
{
            rose_temp__50_inline_20 = lat_rad__45_inline_23 >= cache__43_inline_21 -> lat_min[path_index__44_inline_22] && lat_rad__45_inline_23 <= cache__43_inline_21 -> lat_max[path_index__44_inline_22] && lon_rad__46_inline_24 >= cache__43_inline_21 -> lon_min[path_index__44_inline_22] && lon_rad__46_inline_24 <= cache__43_inline_21 -> lon_max[path_index__44_inline_22];
            goto rose_inline_end__47;
          }
          rose_inline_end__47:
          ;
        }
        rose_temp__49_inline_19 = rose_temp__50_inline_20 == 0;
      }
       else {
        rose_temp__49_inline_19 = 0;
      }
      rose__temp2_inline_18 = ((int )rose_temp__49_inline_19);
      if (rose__temp2_inline_18) {
        continue; 
      }
    }
    if (tile == 0) {
      status = cache_load_path(cache,index,&tile);
      if (status != SIM_OK) {
        return status;
      }
    }
    if (map_tile_contains(tile,lat_rad,lon_rad)) {
      return map_tile_get_height(tile,lat_rad,lon_rad,height_m);
    }
    if ((terrain -> missing_policy) == MAP_MISSING_NEAREST) {
      update_nearest_candidate(tile,lat_rad,lon_rad,&nearest_distance,&nearest_height,&nearest_found);
    }
  }
  terrain -> warning_flags |= 0x00000001U;
  if ((terrain -> missing_policy) == MAP_MISSING_FLAT_FILL) {
     *height_m = terrain -> flat_fill_height_m;
    return SIM_OK;
  }
  if ((terrain -> missing_policy) == MAP_MISSING_NEAREST && nearest_found != 0) {
     *height_m = nearest_height;
    return SIM_OK;
  }
  return SIM_ERR_OUT_OF_RANGE;
}
/** @brief 查找覆盖查询点的瓦片并返回插值高程。 */

SimStatus terrain_get_height(TerrainModel *terrain,double lat_rad,double lon_rad,double *height_m)
{
  size_t index;
  if (terrain == 0 || height_m == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (!__builtin_isfinite(lat_rad) || !__builtin_isfinite(lon_rad)) {
    return SIM_ERR_NUMERIC;
  }
  if (terrain -> enabled == 0) {
     *height_m = terrain -> flat_fill_height_m;
    return SIM_OK;
  }
  if (terrain -> cache != 0) {
    return terrain_get_height_from_cache(terrain,lat_rad,lon_rad,height_m);
  }
  for (index = 1; index <= (terrain -> tile_count + (((long )1) - ((long )(1 + ((size_t )0u))))) / 1; index += 1) {
    if (map_tile_contains(&terrain -> tiles[1 * index + -1LL],lat_rad,lon_rad)) {
      return map_tile_get_height(&terrain -> tiles[1 * index + -1LL],lat_rad,lon_rad,height_m);
    }
  }
  terrain -> warning_flags |= 0x00000001U;
  if ((terrain -> missing_policy) == MAP_MISSING_FLAT_FILL) {
     *height_m = terrain -> flat_fill_height_m;
    return SIM_OK;
  }
  if ((terrain -> missing_policy) == MAP_MISSING_NEAREST && terrain -> tile_count > 0u) {
    const TerrainTile *nearest = 0;
    double nearest_distance = (double )1.79769313486231570814527423731704357e+308L;
    for (index = 0u; index < terrain -> tile_count; ++index) {
      const TerrainTile *tile = &terrain -> tiles[index];
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
      double rose_temp__30;
{
        double value__26 = lat_rad;
        double minimum__27 = tile -> header . lat_min;
        double maximum__28 = tile -> header . lat_max;
        if (value__26 < minimum__27) {{
            rose_temp__30 = minimum__27;
            goto rose_inline_end__29;
          }
        }
        if (value__26 > maximum__28) {{
            rose_temp__30 = maximum__28;
            goto rose_inline_end__29;
          }
        }
{
          rose_temp__30 = value__26;
          goto rose_inline_end__29;
        }
        rose_inline_end__29:
        ;
      }
      const double sample_lat = rose_temp__30;
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
      double rose_temp__36;
{
        double value__32 = lon_rad;
        double minimum__33 = tile -> header . lon_min;
        double maximum__34 = tile -> header . lon_max;
        if (value__32 < minimum__33) {{
            rose_temp__36 = minimum__33;
            goto rose_inline_end__35;
          }
        }
        if (value__32 > maximum__34) {{
            rose_temp__36 = maximum__34;
            goto rose_inline_end__35;
          }
        }
{
          rose_temp__36 = value__32;
          goto rose_inline_end__35;
        }
        rose_inline_end__35:
        ;
      }
      const double sample_lon = rose_temp__36;
      const double dlat = lat_rad - sample_lat;
      const double dlon = lon_rad - sample_lon;
      const double distance = dlat * dlat + dlon * dlon;
      if (distance < nearest_distance) {
        nearest_distance = distance;
        nearest = tile;
      }
    }
    if (nearest != 0) {
      return map_tile_get_height(nearest,(clamp_value(lat_rad,nearest -> header . lat_min,nearest -> header . lat_max)),(clamp_value(lon_rad,nearest -> header . lon_min,nearest -> header . lon_max)),height_m);
    }
  }
  return SIM_ERR_OUT_OF_RANGE;
}
/** @brief 计算椭球高与地形高之差。 */

SimStatus terrain_get_agl(TerrainModel *terrain,const LlaCoord *position,double *agl_m)
{
  double terrain_height;
  SimStatus status;
  if (position == 0 || agl_m == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  status = terrain_get_height(terrain,position -> lat_rad,position -> lon_rad,&terrain_height);
  if (status != SIM_OK) {
    return status;
  }
   *agl_m = position -> height_m - terrain_height;
  return (__builtin_isfinite( *agl_m)?SIM_OK : SIM_ERR_NUMERIC);
}
/** @brief 以 AGL 小于等于零作为地表碰撞判据。 */

SimStatus terrain_is_surface_collision(TerrainModel *terrain,const LlaCoord *position,int *collision)
{
  double agl;
  SimStatus status;
  if (collision == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  status = terrain_get_agl(terrain,position,&agl);
   *collision = (((unsigned int )status) == ((unsigned int )SIM_OK)?agl <= 0.0 :  *collision);
  return status;
}
/** @brief 沿 ECEF 线段内部等距采样并判断是否穿入地形。
 *
 *  @param interior_sample_count 不包含起点和终点的内部采样数量。
 *  该函数不分配内存，适合在导引头更新周期内调用。
 */

SimStatus terrain_line_of_sight_occluded(TerrainModel *terrain,const EarthModel *earth,Vec3 start_ecef_m,Vec3 end_ecef_m,size_t interior_sample_count,int *occluded)
{
  size_t index;
  if (terrain == 0 || earth == 0 || occluded == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (!vec3_isfinite(start_ecef_m) || !vec3_isfinite(end_ecef_m)) {
    return SIM_ERR_NUMERIC;
  }
   *occluded = 0;
  for (index = 1; index <= (interior_sample_count + (((long )1) - ((long )((size_t )1u)))) / 1; index += 1) {
    const double fraction = ((double )(1 * index + 0LL)) / ((double )(interior_sample_count + 1u));
    EcefCoord sample_ecef;
    LlaCoord sample_lla;
    double terrain_height;
    SimStatus status;
    sample_ecef . position_m = vec3_add(start_ecef_m,(vec3_scale((vec3_sub(end_ecef_m,start_ecef_m)),fraction)));
    status = geo_ecef_to_lla(earth,(&sample_ecef),&sample_lla);
    if (status != SIM_OK) {
      return status;
    }
    status = terrain_get_height(terrain,sample_lla . lat_rad,sample_lla . lon_rad,&terrain_height);
    if (status != SIM_OK) {
      return status;
    }
    if (sample_lla . height_m <= terrain_height) {
       *occluded = 1;
      return SIM_OK;
    }
  }
  return SIM_OK;
}
