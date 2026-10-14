/** @file crc32.h
 *  @brief CRC32 数据完整性校验接口。
 *
 *  通信报文和地形瓦片使用同一实现校验载荷。函数不保存全局状态，
 *  因此相同字节序列始终得到相同结果。
 */
#ifndef COMMON_CRC32_H
#define COMMON_CRC32_H

#include <stddef.h>
#include <stdint.h>

/** @brief 计算连续字节数据的 CRC32。
 *
 *  @param data 数据起始地址；当 @p size 为零时允许为空。
 *  @param size 数据长度，单位字节。
 *  @return CRC32 校验值。
 */
uint32_t crc32_compute(const void *data, size_t size);

#endif
