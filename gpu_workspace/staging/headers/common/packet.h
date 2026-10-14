/** @file packet.h
 *  @brief 报文线格式编解码与一致性检查。
 *
 *  线格式采用小端字节序和固定字段宽度，不直接传输 C 结构体内存布局，
 *  从而消除编译器填充和主机字节序差异。
 */
#ifndef COMMON_PACKET_H
#define COMMON_PACKET_H

#include "common/protocol.h"
#include "common/status.h"

#include <stddef.h>
#include <stdint.h>

/** @brief 协议头在线路上的固定字节数。 */
#define SIM_PACKET_HEADER_WIRE_SIZE 36u
/** @brief SensorFrame 载荷在线路上的固定字节数。 */
#define SIM_SENSOR_FRAME_WIRE_SIZE 196u
/** @brief ControlCommand 载荷在线路上的固定字节数。 */
#define SIM_CONTROL_COMMAND_WIRE_SIZE 156u
/** @brief 完整传感器报文最大固定长度。 */
#define SIM_SENSOR_PACKET_WIRE_SIZE (SIM_PACKET_HEADER_WIRE_SIZE + SIM_SENSOR_FRAME_WIRE_SIZE)
/** @brief 完整控制报文最大固定长度。 */
#define SIM_CONTROL_PACKET_WIRE_SIZE (SIM_PACKET_HEADER_WIRE_SIZE + SIM_CONTROL_COMMAND_WIRE_SIZE)
/** @brief 零载荷心跳报文固定长度。 */
#define SIM_HEARTBEAT_PACKET_WIRE_SIZE SIM_PACKET_HEADER_WIRE_SIZE

/** @brief 校验报文头字段是否符合预期。
 *
 *  @param header 已接收的报文头。
 *  @param expected_type 期望的报文类型。
 *  @param expected_instance_id 期望的实例号。
 *  @param expected_payload_size 期望的载荷大小。
 */
SimStatus packet_validate_header(
    const PacketHeader *header,
    PacketType expected_type,
    uint32_t expected_instance_id,
    uint32_t expected_payload_size);

/** @brief 校验载荷完整性。
 *
 *  @param header 已接收的报文头。
 *  @param payload 载荷起始地址。
 */
SimStatus packet_validate_payload(const PacketHeader *header, const void *payload);

/** @brief 将传感器帧编码为稳定线格式。 */
SimStatus packet_encode_sensor_frame(
    uint32_t instance_id,
    const SensorFrame *frame,
    unsigned char *out,
    size_t out_capacity,
    size_t *out_size);
/** @brief 从稳定线格式解码并校验传感器帧。 */
SimStatus packet_decode_sensor_frame(
    const unsigned char *data,
    size_t data_size,
    uint32_t expected_instance_id,
    SensorFrame *out);
/** @brief 将控制指令编码为稳定线格式。 */
SimStatus packet_encode_control_command(
    uint32_t instance_id,
    const ControlCommand *command,
    unsigned char *out,
    size_t out_capacity,
    size_t *out_size);
/** @brief 从稳定线格式解码并校验控制指令。 */
SimStatus packet_decode_control_command(
    const unsigned char *data,
    size_t data_size,
    uint32_t expected_instance_id,
    ControlCommand *out);
/** @brief 编码零载荷应用层心跳报文。 */
SimStatus packet_encode_heartbeat(
    uint32_t instance_id,
    uint32_t seq,
    double sim_time,
    unsigned char *out,
    size_t out_capacity,
    size_t *out_size);
/** @brief 解码并校验零载荷应用层心跳报文。 */
SimStatus packet_decode_heartbeat(
    const unsigned char *data,
    size_t data_size,
    uint32_t expected_instance_id,
    PacketHeader *out_header);

#endif
