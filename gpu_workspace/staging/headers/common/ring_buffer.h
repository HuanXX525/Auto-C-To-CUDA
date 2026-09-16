/** @file ring_buffer.h
 *  @brief 固定容量环形缓冲区。
 *
 *  缓冲区存储由调用方提供，初始化及读写过程均不进行动态内存分配。
 *  该结构可用于传感器延迟线、固定长度历史窗口和进程内消息队列。
 */
#ifndef COMMON_RING_BUFFER_H
#define COMMON_RING_BUFFER_H

#include "common/status.h"
#include "common/device_qualifiers.h"

#include <stddef.h>
#include <stdint.h>

/** @brief 环形缓冲区元数据和运行状态。 */
typedef struct RingBufferView {
    /* Address delta from this view to caller-owned storage. */
    intptr_t data_offset;
    size_t capacity;
    size_t item_size;
    size_t head;
    size_t count;
} RingBufferView;

/** @brief 使用调用方提供的存储初始化环形缓冲区。 */
C2CUDA_HOST_DEVICE SimStatus ring_buffer_init(
    RingBufferView *buffer,
    void *storage,
    size_t capacity,
    size_t item_size);
/** @brief 清空缓冲区但不修改底层存储。 */
C2CUDA_HOST_DEVICE void ring_buffer_clear(RingBufferView *buffer);
/** @brief 返回当前有效元素数量。 */
C2CUDA_HOST_DEVICE size_t ring_buffer_count(const RingBufferView *buffer);
/** @brief 压入元素；缓冲区已满时覆盖最旧元素。 */
C2CUDA_HOST_DEVICE SimStatus ring_buffer_push(RingBufferView *buffer, const void *item);
/** @brief 按从旧到新的逻辑索引读取元素。 */
C2CUDA_HOST_DEVICE SimStatus ring_buffer_get(const RingBufferView *buffer, size_t index_from_oldest, void *out);
/** @brief 按延迟步数读取，0 表示最新元素。 */
C2CUDA_HOST_DEVICE SimStatus ring_buffer_get_delayed(const RingBufferView *buffer, size_t delay_steps, void *out);

#endif
