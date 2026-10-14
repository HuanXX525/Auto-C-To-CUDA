/** @file ring_buffer.c
 *  @brief 固定容量环形缓冲区实现。
 */
#include "common/ring_buffer.h"
#include <stdint.h>
#include <string.h>
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

C2CUDA_HOST_DEVICE static unsigned char *ring_buffer_storage(RingBufferView *buffer)
{
  return (unsigned char *)((uintptr_t )(((uintptr_t )buffer) + ((uintptr_t )(buffer -> data_offset))));
}

C2CUDA_HOST_DEVICE static const unsigned char *ring_buffer_storage_const(const RingBufferView *buffer)
{
  return (const unsigned char *)((uintptr_t )(((uintptr_t )buffer) + ((uintptr_t )(buffer -> data_offset))));
}
/** @brief 绑定调用方存储并复位读写状态。 */

SimStatus ring_buffer_init(RingBufferView *buffer,void *storage,size_t capacity,size_t item_size)
{
  if (buffer == 0 || storage == 0 || capacity == 0u || item_size == 0u) {
    return SIM_ERR_INVALID_ARG;
  }
  if (capacity > 18446744073709551615UL / item_size) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  buffer -> data_offset = ((intptr_t )(((uintptr_t )storage) - ((uintptr_t )buffer)));
  buffer -> capacity = capacity;
  buffer -> item_size = item_size;
  buffer -> head = 0u;
  buffer -> count = 0u;
  return SIM_OK;
}
/** @brief 清除逻辑元素，不擦除底层字节。 */

void ring_buffer_clear(RingBufferView *buffer)
{
  if (buffer != 0) {
    buffer -> head = 0u;
    buffer -> count = 0u;
  }
}
/** @brief 返回当前可读取元素数。 */

size_t ring_buffer_count(const RingBufferView *buffer)
{
  return buffer == 0?0u : buffer -> count;
}
/** @brief 写入元素；容量满时覆盖最旧元素。 */

SimStatus ring_buffer_push(RingBufferView *buffer,const void *item)
{
  unsigned char *destination;
  if (buffer == 0 || item == 0 || buffer -> capacity == 0u || buffer -> item_size == 0u) {
    return SIM_ERR_INVALID_ARG;
  }
  destination = ring_buffer_storage(buffer) + buffer -> head * buffer -> item_size;
  (void )(memcpy(destination,item,buffer -> item_size));
  buffer -> head = (buffer -> head + 1u) % buffer -> capacity;
  if (buffer -> count < buffer -> capacity) {
    ++buffer -> count;
  }
  return SIM_OK;
}
/** @brief 按从旧到新的逻辑序号复制一个元素。 */

SimStatus ring_buffer_get(const RingBufferView *buffer,size_t index_from_oldest,void *out)
{
  size_t oldest;
  size_t physical_index;
  const unsigned char *source;
  if (buffer == 0 || out == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (index_from_oldest >= buffer -> count) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  oldest = (buffer -> head + buffer -> capacity - buffer -> count) % buffer -> capacity;
  physical_index = (oldest + index_from_oldest) % buffer -> capacity;
  source = ring_buffer_storage_const(buffer) + physical_index * buffer -> item_size;
  (void )(memcpy(out,source,buffer -> item_size));
  return SIM_OK;
}
/** @brief 从最新样本向历史方向读取指定延迟步。 */

SimStatus ring_buffer_get_delayed(const RingBufferView *buffer,size_t delay_steps,void *out)
{
  if (buffer == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (delay_steps >= buffer -> count) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  return ring_buffer_get(buffer,buffer -> count - 1u - delay_steps,out);
}
