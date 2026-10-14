/** @file fault_injection_loader.c
 *  @brief 环境程序故障脚本 JSON 解析（host-only 拆分版）。
 *
 *  从原始 fault_injection.c 拆出的 ConfigTree 解析路径，逐行保持原实现；
 *  运行期故障应用（update/apply）在 fault_injection.c 中，可转换为
 *  device 代码。本文件不得进入 device 编译链。
 */
#include "env/fault_injection.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/** @brief 构造 `faults[index].field` 路径。 */
static SimStatus make_fault_path(
    size_t index,
    const char *field,
    char *path,
    size_t path_size)
{
    int written;

    if (field == 0 || path == 0 || path_size == 0u) {
        return SIM_ERR_INVALID_ARG;
    }
    written = snprintf(path, path_size, "faults[%lu].%s", (unsigned long)index, field);
    if (written < 0 || (size_t)written >= path_size) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    return SIM_OK;
}

/** @brief 可选读取字符串字段。 */
static SimStatus get_optional_string(
    const ConfigTree *config,
    size_t index,
    const char *field,
    char *out,
    size_t out_size,
    int *found)
{
    char path[128];
    SimStatus status;

    status = make_fault_path(index, field, path, sizeof(path));
    if (status != SIM_OK) {
        return status;
    }
    status = config_get_string(config, path, out, out_size);
    if (status == SIM_OK) {
        if (found != 0) {
            *found = 1;
        }
        return SIM_OK;
    }
    if (found != 0) {
        *found = 0;
    }
    return SIM_OK;
}

/** @brief 必须读取字符串字段。 */
static SimStatus get_required_string(
    const ConfigTree *config,
    size_t index,
    const char *field,
    char *out,
    size_t out_size)
{
    char path[128];
    SimStatus status;

    status = make_fault_path(index, field, path, sizeof(path));
    if (status != SIM_OK) {
        return status;
    }
    return config_get_string(config, path, out, out_size);
}

/** @brief 可选读取双精度字段。 */
static SimStatus get_optional_double(
    const ConfigTree *config,
    size_t index,
    const char *field,
    double *out,
    int *found)
{
    char path[128];
    SimStatus status;

    status = make_fault_path(index, field, path, sizeof(path));
    if (status != SIM_OK) {
        return status;
    }
    status = config_get_double(config, path, out);
    if (status == SIM_OK) {
        if (found != 0) {
            *found = 1;
        }
        return SIM_OK;
    }
    if (found != 0) {
        *found = 0;
    }
    return SIM_OK;
}

/** @brief 可选读取布尔字段。 */
static SimStatus get_optional_bool(
    const ConfigTree *config,
    size_t index,
    const char *field,
    int *out,
    int *found)
{
    char path[128];
    SimStatus status;

    status = make_fault_path(index, field, path, sizeof(path));
    if (status != SIM_OK) {
        return status;
    }
    status = config_get_bool(config, path, out);
    if (status == SIM_OK) {
        if (found != 0) {
            *found = 1;
        }
        return SIM_OK;
    }
    if (found != 0) {
        *found = 0;
    }
    return SIM_OK;
}

/** @brief 可选读取三轴字段。 */
static SimStatus get_optional_vec3(
    const ConfigTree *config,
    size_t index,
    const char *field,
    Vec3 *out,
    int *found)
{
    char path[128];
    double values[3];
    SimStatus status;

    status = make_fault_path(index, field, path, sizeof(path));
    if (status != SIM_OK) {
        return status;
    }
    status = config_get_double_array(config, path, values, 3u);
    if (status == SIM_OK) {
        *out = vec3_make(values[0], values[1], values[2]);
        if (found != 0) {
            *found = 1;
        }
        return SIM_OK;
    }
    if (found != 0) {
        *found = 0;
    }
    return SIM_OK;
}

/** @brief 解析故障目标字符串。 */
static SimStatus parse_target(const char *text, FaultTarget *target)
{
    if (text == 0 || target == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (strcmp(text, "sensor.frame") == 0 ||
        strcmp(text, "sensor.packet") == 0 ||
        strcmp(text, "communication.sensor_frame") == 0) {
        *target = FAULT_TARGET_SENSOR_FRAME;
    } else if (strcmp(text, "sensor.seeker") == 0) {
        *target = FAULT_TARGET_SENSOR_SEEKER;
    } else if (strcmp(text, "sensor.seeker.range") == 0) {
        *target = FAULT_TARGET_SENSOR_SEEKER_RANGE;
    } else if (strcmp(text, "sensor.seeker.los_unit") == 0) {
        *target = FAULT_TARGET_SENSOR_SEEKER_LOS_UNIT;
    } else if (strcmp(text, "sensor.seeker.los_rate") == 0) {
        *target = FAULT_TARGET_SENSOR_SEEKER_LOS_RATE;
    } else if (strcmp(text, "sensor.seeker.closing_velocity") == 0) {
        *target = FAULT_TARGET_SENSOR_SEEKER_CLOSING_VELOCITY;
    } else if (strcmp(text, "sensor.imu") == 0 ||
        strcmp(text, "sensor.imu.gyro") == 0) {
        *target = FAULT_TARGET_SENSOR_IMU_GYRO;
    } else if (strcmp(text, "sensor.accelerometer") == 0 ||
        strcmp(text, "sensor.accel") == 0) {
        *target = FAULT_TARGET_SENSOR_ACCEL;
    } else if (strcmp(text, "sensor.speedometer") == 0 ||
        strcmp(text, "sensor.speed") == 0) {
        *target = FAULT_TARGET_SENSOR_SPEED;
    } else if (strcmp(text, "actuator.accel_x") == 0 ||
        strcmp(text, "actuator.acceleration[0]") == 0) {
        *target = FAULT_TARGET_ACTUATOR_ACCEL_X;
    } else if (strcmp(text, "actuator.accel_y") == 0 ||
        strcmp(text, "actuator.acceleration[1]") == 0) {
        *target = FAULT_TARGET_ACTUATOR_ACCEL_Y;
    } else if (strcmp(text, "actuator.accel_z") == 0 ||
        strcmp(text, "actuator.acceleration[2]") == 0) {
        *target = FAULT_TARGET_ACTUATOR_ACCEL_Z;
    } else {
        return SIM_ERR_CONFIG;
    }
    return SIM_OK;
}

/** @brief 解析故障类型字符串。 */
static SimStatus parse_type(const char *text, FaultType *type)
{
    if (text == 0 || type == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (strcmp(text, "BIAS") == 0) {
        *type = FAULT_TYPE_BIAS;
    } else if (strcmp(text, "DROPOUT") == 0) {
        *type = FAULT_TYPE_DROPOUT;
    } else if (strcmp(text, "COMMUNICATION_LOSS") == 0 ||
        strcmp(text, "COMM_DROPOUT") == 0 ||
        strcmp(text, "FRAME_DROPOUT") == 0) {
        *type = FAULT_TYPE_DROPOUT;
    } else if (strcmp(text, "FORCE_INVALID") == 0 ||
        strcmp(text, "INVALID") == 0) {
        *type = FAULT_TYPE_FORCE_INVALID;
    } else if (strcmp(text, "STUCK") == 0) {
        *type = FAULT_TYPE_STUCK;
    } else if (strcmp(text, "SCALE") == 0) {
        *type = FAULT_TYPE_SCALE;
    } else if (strcmp(text, "DRIFT") == 0) {
        *type = FAULT_TYPE_DRIFT;
    } else if (strcmp(text, "RAMP_BIAS") == 0 ||
        strcmp(text, "RAMP") == 0) {
        *type = FAULT_TYPE_RAMP_BIAS;
    } else if (strcmp(text, "HOLD_VALUE") == 0 ||
        strcmp(text, "CONSTANT") == 0 ||
        strcmp(text, "STUCK_VALUE") == 0) {
        *type = FAULT_TYPE_HOLD_VALUE;
    } else if (strcmp(text, "COMMUNICATION_DELAY") == 0 ||
        strcmp(text, "COMM_DELAY") == 0 ||
        strcmp(text, "FRAME_DELAY") == 0) {
        *type = FAULT_TYPE_COMMUNICATION_DELAY;
    } else if (strcmp(text, "COMMUNICATION_REORDER") == 0 ||
        strcmp(text, "COMM_REORDER") == 0 ||
        strcmp(text, "FRAME_REORDER") == 0 ||
        strcmp(text, "PACKET_REORDER") == 0) {
        *type = FAULT_TYPE_COMMUNICATION_REORDER;
    } else {
        return SIM_ERR_CONFIG;
    }
    return SIM_OK;
}

/** @brief 返回目标对应的传感器有效位（与运行期文件一致）。 */
static uint32_t target_valid_mask(FaultTarget target)
{
    switch (target) {
    case FAULT_TARGET_SENSOR_FRAME:
        return SIM_SENSOR_VALID_SEEKER |
            SIM_SENSOR_VALID_IMU_GYRO |
            SIM_SENSOR_VALID_ACCEL |
            SIM_SENSOR_VALID_SPEED;
    case FAULT_TARGET_SENSOR_SEEKER:
    case FAULT_TARGET_SENSOR_SEEKER_RANGE:
    case FAULT_TARGET_SENSOR_SEEKER_LOS_UNIT:
    case FAULT_TARGET_SENSOR_SEEKER_LOS_RATE:
    case FAULT_TARGET_SENSOR_SEEKER_CLOSING_VELOCITY:
        return SIM_SENSOR_VALID_SEEKER;
    case FAULT_TARGET_SENSOR_IMU_GYRO:
        return SIM_SENSOR_VALID_IMU_GYRO;
    case FAULT_TARGET_SENSOR_ACCEL:
        return SIM_SENSOR_VALID_ACCEL;
    case FAULT_TARGET_SENSOR_SPEED:
        return SIM_SENSOR_VALID_SPEED;
    default:
        return 0u;
    }
}

/** @brief 目标是否为虚拟执行机构（与运行期文件一致）。 */
static int target_actuator_index(FaultTarget target, size_t *index)
{
    if (index == 0) {
        return 0;
    }
    if (target == FAULT_TARGET_ACTUATOR_ACCEL_X) {
        *index = 0u;
        return 1;
    }
    if (target == FAULT_TARGET_ACTUATOR_ACCEL_Y) {
        *index = 1u;
        return 1;
    }
    if (target == FAULT_TARGET_ACTUATOR_ACCEL_Z) {
        *index = 2u;
        return 1;
    }
    return 0;
}

/** @brief 判断故障是否属于通信层传感器帧故障（与运行期文件一致）。 */
static int fault_is_sensor_frame_communication(const FaultDefinition *fault)
{
    if (fault == 0 || fault->target != FAULT_TARGET_SENSOR_FRAME) {
        return 0;
    }
    return fault->type == FAULT_TYPE_DROPOUT ||
        fault->type == FAULT_TYPE_FORCE_INVALID ||
        fault->type == FAULT_TYPE_COMMUNICATION_DELAY ||
        fault->type == FAULT_TYPE_COMMUNICATION_REORDER;
}

/** @brief 校验故障类型和目标对象是否匹配。 */
static int fault_type_matches_target(const FaultDefinition *fault)
{
    size_t actuator_index;

    if (fault == 0) {
        return 0;
    }
    if (fault->type == FAULT_TYPE_DROPOUT ||
        fault->type == FAULT_TYPE_FORCE_INVALID) {
        return target_valid_mask(fault->target) != 0u;
    }
    if (fault->type == FAULT_TYPE_COMMUNICATION_DELAY) {
        return fault->target == FAULT_TARGET_SENSOR_FRAME;
    }
    if (fault->type == FAULT_TYPE_COMMUNICATION_REORDER) {
        return fault->target == FAULT_TARGET_SENSOR_FRAME;
    }
    if (fault->type == FAULT_TYPE_STUCK ||
        fault->type == FAULT_TYPE_SCALE) {
        return target_actuator_index(fault->target, &actuator_index);
    }
    if (fault->type == FAULT_TYPE_HOLD_VALUE) {
        switch (fault->target) {
        case FAULT_TARGET_SENSOR_SEEKER_RANGE:
        case FAULT_TARGET_SENSOR_SEEKER_LOS_UNIT:
        case FAULT_TARGET_SENSOR_SEEKER_LOS_RATE:
        case FAULT_TARGET_SENSOR_SEEKER_CLOSING_VELOCITY:
        case FAULT_TARGET_SENSOR_IMU_GYRO:
        case FAULT_TARGET_SENSOR_ACCEL:
        case FAULT_TARGET_SENSOR_SPEED:
            return 1;
        default:
            return 0;
        }
    }
    switch (fault->target) {
    case FAULT_TARGET_SENSOR_SEEKER_RANGE:
    case FAULT_TARGET_SENSOR_SEEKER_LOS_UNIT:
    case FAULT_TARGET_SENSOR_SEEKER_LOS_RATE:
    case FAULT_TARGET_SENSOR_SEEKER_CLOSING_VELOCITY:
    case FAULT_TARGET_SENSOR_IMU_GYRO:
    case FAULT_TARGET_SENSOR_ACCEL:
    case FAULT_TARGET_SENSOR_SPEED:
        return 1;
    default:
        return 0;
    }
}

SimStatus fault_injection_load_config(const ConfigTree *config, FaultInjection *faults)
{
    size_t count;
    size_t index;
    SimStatus status;

    if (config == 0 || faults == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    fault_injection_init_empty(faults);
    status = config_validate_schema(config, 1u);
    if (status != SIM_OK) {
        return status;
    }
    status = config_get_array_count(config, "faults", &count);
    if (status != SIM_OK) {
        return status;
    }
    if (count > ENV_MAX_FAULTS) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    faults->enabled = 1;
    faults->fault_count = count;
    for (index = 0u; index < count; ++index) {
        FaultDefinition *fault = &faults->faults[index];
        double scalar = 0.0;
        Vec3 vector = vec3_make(0.0, 0.0, 0.0);
        int found = 0;

        fault->enabled = 1;
        (void)snprintf(fault->id, sizeof(fault->id), "fault_%02lu", (unsigned long)index);
        status = get_optional_string(
            config,
            index,
            "id",
            fault->id,
            sizeof(fault->id),
            &found);
        if (status != SIM_OK) {
            return status;
        }
        status = get_optional_bool(config, index, "enabled", &fault->enabled, &found);
        if (status != SIM_OK) {
            return status;
        }
        status = get_required_string(
            config,
            index,
            "target",
            fault->target_text,
            sizeof(fault->target_text));
        if (status != SIM_OK) {
            return status;
        }
        status = get_required_string(
            config,
            index,
            "type",
            fault->type_text,
            sizeof(fault->type_text));
        if (status != SIM_OK) {
            return status;
        }
        status = parse_target(fault->target_text, &fault->target);
        if (status == SIM_OK) {
            status = parse_type(fault->type_text, &fault->type);
        }
        if (status != SIM_OK) {
            return status;
        }

        status = get_optional_double(config, index, "start_time_s", &fault->start_time_s, &found);
        if (status != SIM_OK) {
            return status;
        }
        if (found == 0) {
            status = get_optional_double(config, index, "time_s", &fault->start_time_s, &found);
            if (status != SIM_OK || found == 0) {
                return status == SIM_OK ? SIM_ERR_CONFIG : status;
            }
        }
        status = get_optional_double(config, index, "duration_s", &fault->duration_s, &found);
        if (status != SIM_OK || found == 0) {
            return status == SIM_OK ? SIM_ERR_CONFIG : status;
        }
        status = get_optional_double(config, index, "ramp_in_s", &fault->ramp_in_s, &found);
        if (status != SIM_OK) {
            return status;
        }
        if (found == 0) {
            fault->ramp_in_s = 0.0;
        }
        status = get_optional_double(config, index, "recovery_ramp_s", &fault->recovery_ramp_s, &found);
        if (status != SIM_OK) {
            return status;
        }
        if (found == 0) {
            fault->recovery_ramp_s = 0.0;
        }
        status = get_optional_double(config, index, "recovery_hold_s", &fault->recovery_hold_s, &found);
        if (status != SIM_OK) {
            return status;
        }
        if (found == 0) {
            status = get_optional_double(config, index, "recovery_timeout_s", &fault->recovery_hold_s, &found);
            if (status != SIM_OK) {
                return status;
            }
        }
        if (found == 0) {
            fault->recovery_hold_s = 0.0;
        }
        status = get_optional_double(config, index, "value", &scalar, &found);
        if (status != SIM_OK) {
            return status;
        }
        fault->scalar_value = found != 0 ? scalar : 0.0;
        vector = vec3_make(fault->scalar_value, fault->scalar_value, fault->scalar_value);
        status = get_optional_vec3(config, index, "value_xyz", &vector, &found);
        if (status != SIM_OK) {
            return status;
        }
        fault->vector_value = vector;
        status = get_optional_double(config, index, "scale", &fault->scale, &found);
        if (status != SIM_OK) {
            return status;
        }
        if (found == 0) {
            fault->scale = fault->scalar_value;
        }
        if (fault->type == FAULT_TYPE_COMMUNICATION_DELAY) {
            if (fault->scalar_value < 1.0 ||
                fault->scalar_value > (double)ENV_MAX_COMMUNICATION_DELAY_STEPS ||
                floor(fault->scalar_value) != fault->scalar_value) {
                return SIM_ERR_OUT_OF_RANGE;
            }
            fault->delay_steps = (unsigned int)fault->scalar_value;
        }
        if (!isfinite(fault->start_time_s) || fault->start_time_s < 0.0 ||
            !isfinite(fault->duration_s) || fault->duration_s <= 0.0 ||
            !isfinite(fault->ramp_in_s) || fault->ramp_in_s < 0.0 ||
            !isfinite(fault->recovery_ramp_s) || fault->recovery_ramp_s < 0.0 ||
            !isfinite(fault->recovery_hold_s) || fault->recovery_hold_s < 0.0 ||
            !isfinite(fault->scalar_value) ||
            !vec3_isfinite(fault->vector_value) ||
            !isfinite(fault->scale)) {
            return SIM_ERR_OUT_OF_RANGE;
        }
        if (fault->recovery_hold_s > 0.0 && fault_is_sensor_frame_communication(fault) == 0) {
            return SIM_ERR_CONFIG;
        }
        if (!fault_type_matches_target(fault)) {
            return SIM_ERR_CONFIG;
        }
    }
    return SIM_OK;
}
