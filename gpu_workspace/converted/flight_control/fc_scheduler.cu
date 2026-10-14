/** @file fc_scheduler.c
 *  @brief 飞控周期任务调度视图实现。
 */
#include "fc/fc_scheduler.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

SimStatus fc_scheduler_init(FcScheduler *scheduler,double base_rate_hz)
{
  if (scheduler == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (!__builtin_isfinite(base_rate_hz) || base_rate_hz <= 0.0) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  scheduler -> base_rate_hz = base_rate_hz;
  scheduler -> tick = 0u;
  scheduler -> task_count = 0u;
  return SIM_OK;
}

SimStatus fc_scheduler_add_task(FcScheduler *scheduler,const char *name,uint32_t period_ticks)
{
  FcTask *task;
  size_t name_index;
  if (scheduler == 0 || name == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (period_ticks == 0u) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  if (scheduler -> task_count >= 8u) {
    return SIM_ERR_OUT_OF_RANGE;
  }
  task = &scheduler -> tasks[scheduler -> task_count++];
  for (name_index = 0u; name_index + 1u < sizeof(task -> name) && name[name_index] != '\0'; ++name_index) {
    task -> name[name_index] = name[name_index];
  }
  task -> name[name_index] = '\0';
  task -> period_ticks = period_ticks;
  task -> last_run_tick = 0u;
  return SIM_OK;
}

int fc_scheduler_task_due(const FcScheduler *scheduler,uint32_t task_index)
{
  const FcTask *task;
  if (scheduler == 0 || task_index >= scheduler -> task_count) {
    return 0;
  }
  task = &scheduler -> tasks[task_index];
  return task -> period_ticks != 0u && scheduler -> tick % task -> period_ticks == 0u;
}

int fc_scheduler_task_due_name(const FcScheduler *scheduler,const char *name)
{
  uint32_t index;
  size_t name_index;
  int equal;
  if (scheduler == 0 || name == 0) {
    return 0;
  }
  for (index = 0u; index < scheduler -> task_count; ++index) {
    equal = 1;
    for (name_index = 0u; name_index < sizeof(scheduler -> tasks[index] . name); ++name_index) {
      if (scheduler -> tasks[index] . name[name_index] != name[name_index]) {
        equal = 0;
        break; 
      }
      if (name[name_index] == '\0') {
        break; 
      }
    }
    if (equal != 0) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
      int rose_temp__15;
{
        const FcScheduler *scheduler__12 = scheduler;
        uint32_t task_index__13 = index;
        const FcTask *task;
        if (scheduler__12 == 0 || task_index__13 >= scheduler__12 -> task_count) {{
            rose_temp__15 = 0;
            goto rose_inline_end__14;
          }
        }
        task = &scheduler__12 -> tasks[task_index__13];
{
          rose_temp__15 = task -> period_ticks != 0u && scheduler__12 -> tick % task -> period_ticks == 0u;
          goto rose_inline_end__14;
        }
        rose_inline_end__14:
        ;
      }
      return rose_temp__15;
    }
  }
  return 0;
}

void fc_scheduler_advance(FcScheduler *scheduler)
{
  if (scheduler != 0) {
    ++scheduler -> tick;
  }
}
