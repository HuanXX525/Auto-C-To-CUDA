/** @file fc_state.c
 *  @brief 单实例飞控控制器实现。
 */
#include "fc/fc_state.h"
#include <string.h>
/** @brief 按当前单帧判定快速推进启动和工作模式。 */
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

C2CUDA_HOST_DEVICE static FcMode advance_mode(FcMode current,const FcModeInput *input)
{
  FcMode mode = current;
  unsigned int iteration;
  for (iteration = 0u; iteration < 6u; ++iteration) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
    FcMode rose_temp__10;
{
      FcMode current__7 = mode;
      const FcModeInput *input__8 = input;
      if (input__8 == 0) {{
          rose_temp__10 = FC_FAULT;
          goto rose_inline_end__9;
        }
      }
      if (input__8 -> shutdown != 0) {{
          rose_temp__10 = FC_SHUTDOWN;
          goto rose_inline_end__9;
        }
      }
      if (input__8 -> fault != 0 || input__8 -> self_test_ok == 0) {{
          rose_temp__10 = FC_FAULT;
          goto rose_inline_end__9;
        }
      }
      if (input__8 -> degraded != 0) {{
          rose_temp__10 = FC_DEGRADED;
          goto rose_inline_end__9;
        }
      }
      if (input__8 -> command_hold != 0) {
        if (current__7 == FC_POWER_ON || current__7 == FC_SELF_TEST) {{
            rose_temp__10 = ((current__7 == FC_POWER_ON?FC_SELF_TEST : FC_WAIT_SENSOR));
            goto rose_inline_end__9;
          }
        }
{
          rose_temp__10 = FC_COMMAND_HOLD;
          goto rose_inline_end__9;
        }
      }
      switch(current__7){
        case FC_POWER_ON:
{
          rose_temp__10 = FC_SELF_TEST;
          goto rose_inline_end__9;
        }
        case FC_SELF_TEST:
{
          rose_temp__10 = FC_WAIT_SENSOR;
          goto rose_inline_end__9;
        }
        case FC_WAIT_SENSOR:
{
          rose_temp__10 = ((input__8 -> sensor_frame_valid != 0?FC_NAV_READY : FC_WAIT_SENSOR));
          goto rose_inline_end__9;
        }
        case FC_NAV_READY:
{
          rose_temp__10 = ((input__8 -> navigation_valid != 0?FC_GUIDANCE_STANDBY : FC_WAIT_SENSOR));
          goto rose_inline_end__9;
        }
        case FC_GUIDANCE_STANDBY:
{
          rose_temp__10 = ((input__8 -> guidance_valid != 0?FC_GUIDANCE_ACTIVE : FC_COMMAND_HOLD));
          goto rose_inline_end__9;
        }
        case FC_COMMAND_HOLD:;
        case FC_DEGRADED:
        if (input__8 -> guidance_valid != 0 && input__8 -> navigation_valid != 0) {{
            rose_temp__10 = FC_GUIDANCE_ACTIVE;
            goto rose_inline_end__9;
          }
        }
{
          rose_temp__10 = current__7;
          goto rose_inline_end__9;
        }
        case FC_GUIDANCE_ACTIVE:
        if (input__8 -> guidance_valid == 0) {{
            rose_temp__10 = FC_COMMAND_HOLD;
            goto rose_inline_end__9;
          }
        }
{
          rose_temp__10 = FC_GUIDANCE_ACTIVE;
          goto rose_inline_end__9;
        }
        case FC_FAULT:;
        case FC_SHUTDOWN:;
        default:
{
          rose_temp__10 = current__7;
          goto rose_inline_end__9;
        }
      }
      rose_inline_end__9:
      ;
    }
    const FcMode next = rose_temp__10;
    if (next == mode) {
      break; 
    }
    mode = next;
    if (mode == FC_GUIDANCE_ACTIVE || mode == FC_COMMAND_HOLD || mode == FC_DEGRADED || mode == FC_FAULT || mode == FC_SHUTDOWN) {
      break; 
    }
  }
  return mode;
}
/** @brief 注册默认飞控任务表，保持旧配置的每帧执行行为。 */

C2CUDA_HOST_DEVICE static SimStatus register_default_tasks(FcScheduler *scheduler)
{
  SimStatus status;
  status = fc_scheduler_add_task(scheduler,"receive",1u);
  if (status == SIM_OK) {
    status = fc_scheduler_add_task(scheduler,"navigation",1u);
  }
  if (status == SIM_OK) {
    status = fc_scheduler_add_task(scheduler,"guidance",1u);
  }
  if (status == SIM_OK) {
    status = fc_scheduler_add_task(scheduler,"controller",1u);
  }
  if (status == SIM_OK) {
    status = fc_scheduler_add_task(scheduler,"safety",1u);
  }
  return status;
}
/** @brief 按配置注册飞控任务表。 */

C2CUDA_HOST_DEVICE static SimStatus register_configured_tasks(FcScheduler *scheduler,const FlightControllerConfig *config)
{
  uint32_t index;
  SimStatus status = SIM_OK;
  if (scheduler == 0 || config == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (config -> scheduler_task_count == 0u) {
    return register_default_tasks(scheduler);
  }
  for (index = 0u; index < config -> scheduler_task_count && status == SIM_OK; ++index) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;{
      FcScheduler *scheduler__2_inline_1 = scheduler;
      const char *name__3_inline_2 = config -> scheduler_tasks[index] . name;
      uint32_t period_ticks__4_inline_3 = config -> scheduler_tasks[index] . period_ticks;
      FcTask *task_inline_4;
      size_t name_index_inline_5;
      if (scheduler__2_inline_1 == 0 || name__3_inline_2 == 0) {{
          status = SIM_ERR_INVALID_ARG;
          goto rose_inline_end__5;
        }
      }
      if (period_ticks__4_inline_3 == 0u) {{
          status = SIM_ERR_OUT_OF_RANGE;
          goto rose_inline_end__5;
        }
      }
      if (scheduler__2_inline_1 -> task_count >= 8u) {{
          status = SIM_ERR_OUT_OF_RANGE;
          goto rose_inline_end__5;
        }
      }
      task_inline_4 = &scheduler__2_inline_1 -> tasks[scheduler__2_inline_1 -> task_count++];
      for (name_index_inline_5 = 0u; name_index_inline_5 + 1u < sizeof(task_inline_4 -> name) && name__3_inline_2[name_index_inline_5] != '\0'; ++name_index_inline_5) {
        task_inline_4 -> name[name_index_inline_5] = name__3_inline_2[name_index_inline_5];
      }
      task_inline_4 -> name[name_index_inline_5] = '\0';
      task_inline_4 -> period_ticks = period_ticks__4_inline_3;
      task_inline_4 -> last_run_tick = 0u;
{
        status = SIM_OK;
        goto rose_inline_end__5;
      }
      rose_inline_end__5:
      ;
    }
  }
  return status;
}

SimStatus flight_controller_init(FlightController *controller,const FlightControllerConfig *config)
{
  CommandManagerConfig command_config;
  SimStatus status;
  if (controller == 0 || config == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  (void )(memset(controller,0,sizeof(( *controller))));
  controller -> mode = FC_POWER_ON;
  status = fc_scheduler_init(&controller -> scheduler,config -> scheduler_base_rate_hz);
  if (status == SIM_OK) {
    status = register_configured_tasks(&controller -> scheduler,config);
  }
  if (status == SIM_OK) {
    status = estimator_init(&controller -> estimator);
  }
  if (status == SIM_OK) {
    status = guidance_manager_init(&controller -> guidance,&config -> guidance);
  }
  if (status == SIM_OK) {
    status = autopilot_init(&controller -> autopilot,&config -> autopilot);
  }
  if (status == SIM_OK) {
    status = safety_monitor_init(&controller -> safety,&config -> safety);
  }
  if (status == SIM_OK) {
    command_config . max_accel_mps2 = config -> guidance . max_accel_mps2;
    command_config . max_accel_rate_mps3 = config -> guidance . max_accel_rate_mps3;
    command_config . command_hold_s = config -> safety . command_hold_s;
    status = command_manager_init(&controller -> command_manager,(&command_config));
  }
  return status;
}

SimStatus flight_controller_step(FlightController *controller,const SensorFrame *sensor,ControlCommand *command)
{
  SafetyAssessment assessment;
  NavState nav;
  GuidancePngOutput guidance;
  AutopilotCommand autopilot_command;
  FcModeInput mode_input;
  uint32_t status_flags;
  int guidance_valid = 0;
  int nav_valid = 0;
  int request_hold = 0;
  int navigation_due;
  int guidance_due;
  int controller_due;
  SimStatus status;
  if (controller == 0 || sensor == 0 || command == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  fc_health_reset(&controller -> health);
  status = safety_monitor_check_sensor(&controller -> safety,sensor,controller -> have_seq,controller -> last_seq,controller -> have_last_sensor_time,controller -> last_sensor_time,&assessment);
  if (status != SIM_OK) {
    return status;
  }
  status_flags = assessment . status_flags;
  request_hold = assessment . request_hold;
  autopilot_zero_command(&autopilot_command);
  (void )(memset((&nav),0,sizeof(nav)));
  (void )(memset((&guidance),0,sizeof(guidance)));
  if (assessment . accept_frame != 0) {
    navigation_due = fc_scheduler_task_due_name((&controller -> scheduler),"navigation");
    guidance_due = fc_scheduler_task_due_name((&controller -> scheduler),"guidance");
    controller_due = fc_scheduler_task_due_name((&controller -> scheduler),"controller");
    if (navigation_due != 0 || controller -> have_last_nav == 0) {
      status = estimator_update(&controller -> estimator,sensor,&nav);
      if (status == SIM_OK) {
        controller -> last_nav = nav;
        controller -> have_last_nav = 1;
      }
    }
     else {
      nav = controller -> last_nav;
      status = SIM_OK;
    }
    if (status == SIM_OK) {
      nav_valid = (nav . valid_flags & 0x00000001U) != 0u;
      if (navigation_has_guidance_solution((&nav))) {
        if (guidance_due != 0 || controller -> have_last_guidance == 0) {
          status = guidance_manager_update(&controller -> guidance,(&nav),&guidance);
          if (status == SIM_OK) {
            controller -> last_guidance = guidance;
            controller -> have_last_guidance = 1;
          }
        }
         else {
          guidance = controller -> last_guidance;
          status = SIM_OK;
        }
        if (status == SIM_OK && controller -> have_last_guidance != 0) {
          if (controller_due != 0 || controller -> have_last_autopilot_command == 0) {
            status = autopilot_update(&controller -> autopilot,(&nav),(&guidance),&autopilot_command);
            if (status == SIM_OK) {
              controller -> last_autopilot_command = autopilot_command;
              controller -> have_last_autopilot_command = 1;
            }
          }
           else {
            autopilot_command = controller -> last_autopilot_command;
            status = SIM_OK;
          }
          guidance_valid = status == SIM_OK && controller -> have_last_autopilot_command != 0;
        }
      }
    }
    if (status != SIM_OK) {
      if (status == SIM_ERR_NUMERIC) {
        status_flags |= 0x80000000U;
      }
       else {
        status_flags |= 0x00000008U;
      }
      request_hold = 1;
    }
  }
  (void )(memset((&mode_input),0,sizeof(mode_input)));
  mode_input . self_test_ok = 1;
  mode_input . sensor_frame_valid = assessment . accept_frame;
  mode_input . navigation_valid = nav_valid;
  mode_input . guidance_valid = guidance_valid;
  mode_input . command_hold = request_hold;
  mode_input . degraded = assessment . degraded;
  mode_input . fault = assessment . fault || (status_flags & 0x80000000U) != 0u;
  controller -> mode = advance_mode(controller -> mode,(&mode_input));
  if ((status_flags & 0x80000000U) != 0u) {
    fc_health_set_fault(&controller -> health,status_flags & 0x80000000U);
  }
  fc_health_set_warning(&controller -> health,status_flags & ~0x80000000U);
  status_flags = fc_health_command_status((&controller -> health));
  status = command_manager_build(&controller -> command_manager,sensor -> seq,sensor -> sim_time,sensor -> dt,controller -> mode,status_flags,request_hold || guidance_valid == 0,((guidance_valid != 0?&autopilot_command : 0)),command,&status_flags);
  if (status != SIM_OK) {
    return status;
  }
  command -> command_mode = ((uint32_t )(controller -> mode));
  command -> command_status = status_flags;
  controller -> state . last_sensor =  *sensor;
  controller -> state . last_command =  *command;
  if (assessment . accept_frame != 0) {
    controller -> last_seq = sensor -> seq;
    controller -> have_seq = 1;
    controller -> last_sensor_time = sensor -> sim_time;
    controller -> have_last_sensor_time = 1;
  }
  fc_scheduler_advance(&controller -> scheduler);
  return SIM_OK;
}
