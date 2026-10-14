/** @file protocol.h
 *  @brief 仿真进程间通信协议。
 *
 *  飞控程序和环境程序通过 UDP 交换二进制报文。该头文件定义统一的
 *  报文头、传感器帧和控制指令格式，以及所有实例共享的协议常量。
 */
#ifndef COMMON_PROTOCOL_H
#define COMMON_PROTOCOL_H

#include "common/vec3.h"

#include <stdint.h>

/** @brief 报文魔数，用于快速识别协议帧。 */
#define SIM_PACKET_MAGIC 0x53494D31u
/** @brief 单个控制实例允许的执行机构数量上限。 */
#define SIM_MAX_ACTUATORS 8u

/** @brief SensorFrame 中导引头测量有效。 */
#define SIM_SENSOR_VALID_SEEKER UINT32_C(0x00000001)
/** @brief SensorFrame 中陀螺仪测量有效。 */
#define SIM_SENSOR_VALID_IMU_GYRO UINT32_C(0x00000002)
/** @brief SensorFrame 中加速度计测量有效。 */
#define SIM_SENSOR_VALID_ACCEL UINT32_C(0x00000004)
/** @brief SensorFrame 中速度计测量有效。 */
#define SIM_SENSOR_VALID_SPEED UINT32_C(0x00000008)
/** @brief SensorFrame 中大地坐标和高度测量有效。 */
#define SIM_SENSOR_VALID_GEODETIC UINT32_C(0x00000010)

/** @brief 导引头视线被地形遮挡。 */
#define SIM_SENSOR_FAULT_LOS_OCCLUDED UINT32_C(0x00000001)
/** @brief 导引头样本被丢弃。 */
#define SIM_SENSOR_FAULT_SEEKER_DROPOUT UINT32_C(0x00000002)
/** @brief IMU 陀螺仪样本被丢弃。 */
#define SIM_SENSOR_FAULT_IMU_DROPOUT UINT32_C(0x00000004)
/** @brief 加速度计样本被丢弃。 */
#define SIM_SENSOR_FAULT_ACCEL_DROPOUT UINT32_C(0x00000008)
/** @brief 速度计样本被丢弃。 */
#define SIM_SENSOR_FAULT_SPEED_DROPOUT UINT32_C(0x00000010)
/** @brief 至少一个传感器延迟线尚未积累足够历史样本。 */
#define SIM_SENSOR_FAULT_DELAY_WARMUP UINT32_C(0x00000020)

/** @brief 报文类型枚举。
 *
 *  报文类型用于区分传感器输出、控制命令、心跳和事件消息。
 */
typedef enum PacketType {
    PACKET_SENSOR_FRAME = 1,
    PACKET_CONTROL_COMMAND = 2,
    PACKET_HEARTBEAT = 3,
    PACKET_SIM_CONTROL = 4,
    PACKET_EVENT = 5
} PacketType;

/** @brief 协议报文头。
 *
 *  所有报文都以该头开始。头部包含版本、实例号、序列号、仿真时间和
 *  载荷校验信息，用于实现跨进程的数据完整性检查。
 */
typedef struct PacketHeader {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t type;
    uint16_t header_size;
    uint32_t instance_id;
    uint32_t seq;
    double sim_time;
    uint32_t payload_size;
    uint32_t payload_crc32;
} PacketHeader;

/** @brief 环境程序发送给飞控程序的传感器帧。
 *
 *  该结构承载 IMU、速度计、加速度计、测距、视线方向和闭合速度等信息。
 *  所有测量值都应明确对应的坐标系和单位，便于飞控侧做状态估计。
 */
typedef struct SensorFrame {
    uint32_t seq;
    double sim_time;
    double dt;
    Vec3 missile_vel_ecef_meas;
    Vec3 missile_accel_ecef_meas;
    Vec3 missile_gyro_b_meas;
    double missile_lat_rad_meas;
    double missile_lon_rad_meas;
    double missile_height_m_meas;
    double missile_height_agl_m_meas;
    double target_range_meas;
    Vec3 target_los_unit_ecef_meas;
    Vec3 target_los_rate_ecef_meas;
    double target_closing_velocity_meas;
    uint32_t sensor_valid_flags;
    uint32_t sensor_fault_flags;
} SensorFrame;

/** @brief 飞控程序输出给环境程序的控制指令。
 *
 *  该结构既支持加速度指令，也预留了姿态、角速度和执行机构通道。
 *  环境程序可按模型复杂度选择性解释其中字段。
 */
typedef struct ControlCommand {
    uint32_t seq;
    double sim_time;
    Vec3 accel_cmd_ecef;
    Vec3 attitude_cmd;
    Vec3 body_rate_cmd;
    double actuator_cmd[SIM_MAX_ACTUATORS];
    uint32_t command_mode;
    uint32_t command_status;
} ControlCommand;

/** @brief 构造报文头。 */
PacketHeader packet_header_make(
    PacketType type,
    uint32_t instance_id,
    uint32_t seq,
    double sim_time,
    uint32_t payload_size,
    uint32_t payload_crc32);

#endif
