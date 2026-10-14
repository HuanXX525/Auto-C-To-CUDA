/** @file fc_health.c
 *  @brief 飞控健康状态工具函数实现。
 */
#include "fc/fc_health.h"
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

void fc_health_reset(FcHealth *health)
{
  if (health != 0) {
    health -> fault_flags = 0u;
    health -> warning_flags = 0u;
  }
}

void fc_health_set_warning(FcHealth *health,uint32_t flags)
{
  if (health != 0) {
    health -> warning_flags |= flags;
  }
}

void fc_health_set_fault(FcHealth *health,uint32_t flags)
{
  if (health != 0) {
    health -> fault_flags |= flags;
  }
}

int fc_health_has_fault(const FcHealth *health)
{
  return health != 0 && health -> fault_flags != 0u;
}

uint32_t fc_health_command_status(const FcHealth *health)
{
  if (health == 0) {
    return 0x80000000U;
  }
  return health -> warning_flags | health -> fault_flags;
}
