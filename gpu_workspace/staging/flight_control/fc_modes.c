/** @file fc_modes.c
 *  @brief 飞控模式状态机实现。
 */
#include "fc/fc_modes.h"

FcMode fc_mode_next(FcMode current, const FcModeInput *input)
{
    if (input == 0) {
        return FC_FAULT;
    }
    if (input->shutdown != 0) {
        return FC_SHUTDOWN;
    }
    if (input->fault != 0 || input->self_test_ok == 0) {
        return FC_FAULT;
    }
    if (input->degraded != 0) {
        return FC_DEGRADED;
    }
    if (input->command_hold != 0) {
        if (current == FC_POWER_ON || current == FC_SELF_TEST) {
            return current == FC_POWER_ON ? FC_SELF_TEST : FC_WAIT_SENSOR;
        }
        return FC_COMMAND_HOLD;
    }

    switch (current) {
    case FC_POWER_ON:
        return FC_SELF_TEST;
    case FC_SELF_TEST:
        return FC_WAIT_SENSOR;
    case FC_WAIT_SENSOR:
        return input->sensor_frame_valid != 0 ? FC_NAV_READY : FC_WAIT_SENSOR;
    case FC_NAV_READY:
        return input->navigation_valid != 0 ? FC_GUIDANCE_STANDBY : FC_WAIT_SENSOR;
    case FC_GUIDANCE_STANDBY:
        return input->guidance_valid != 0 ? FC_GUIDANCE_ACTIVE : FC_COMMAND_HOLD;
    case FC_COMMAND_HOLD:
    case FC_DEGRADED:
        if (input->guidance_valid != 0 && input->navigation_valid != 0) {
            return FC_GUIDANCE_ACTIVE;
        }
        return current;
    case FC_GUIDANCE_ACTIVE:
        if (input->guidance_valid == 0) {
            return FC_COMMAND_HOLD;
        }
        return FC_GUIDANCE_ACTIVE;
    case FC_FAULT:
    case FC_SHUTDOWN:
    default:
        return current;
    }
}

const char *fc_mode_to_string(FcMode mode)
{
    switch (mode) {
    case FC_POWER_ON:
        return "FC_POWER_ON";
    case FC_SELF_TEST:
        return "FC_SELF_TEST";
    case FC_WAIT_SENSOR:
        return "FC_WAIT_SENSOR";
    case FC_NAV_READY:
        return "FC_NAV_READY";
    case FC_GUIDANCE_STANDBY:
        return "FC_GUIDANCE_STANDBY";
    case FC_GUIDANCE_ACTIVE:
        return "FC_GUIDANCE_ACTIVE";
    case FC_COMMAND_HOLD:
        return "FC_COMMAND_HOLD";
    case FC_DEGRADED:
        return "FC_DEGRADED";
    case FC_FAULT:
        return "FC_FAULT";
    case FC_SHUTDOWN:
        return "FC_SHUTDOWN";
    default:
        return "FC_UNKNOWN";
    }
}
