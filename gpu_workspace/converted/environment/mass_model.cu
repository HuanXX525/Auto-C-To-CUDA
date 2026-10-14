/** @file mass_model.c
 *  @brief 干质量和推进剂消耗模型实现。
 */
#include "env/mass_model.h"
#include <math.h>
/** @brief 初始化质量组成并计算总质量。 */
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

SimStatus mass_model_init(MassModel *model,double dry_mass_kg,double propellant_mass_kg)
{
  if (model == 0 || !__builtin_isfinite(dry_mass_kg) || !__builtin_isfinite(propellant_mass_kg) || dry_mass_kg <= 0.0 || propellant_mass_kg < 0.0) {
    return SIM_ERR_INVALID_ARG;
  }
  model -> dry_mass_kg = dry_mass_kg;
  model -> propellant_mass_kg = propellant_mass_kg;
  model -> mass_kg = dry_mass_kg + propellant_mass_kg;
  return SIM_OK;
}
/** @brief 消耗推进剂，且保证总质量不会低于干质量。 */

SimStatus mass_model_step(MassModel *model,double mass_flow_kgps,double dt)
{
  double consumed;
  if (model == 0 || !__builtin_isfinite(mass_flow_kgps) || !__builtin_isfinite(dt) || mass_flow_kgps < 0.0 || dt < 0.0 || model -> dry_mass_kg <= 0.0 || model -> propellant_mass_kg < 0.0) {
    return SIM_ERR_INVALID_ARG;
  }
  consumed = mass_flow_kgps * dt;
  consumed = (consumed > model -> propellant_mass_kg?model -> propellant_mass_kg : consumed);
  model -> propellant_mass_kg -= consumed;
  model -> mass_kg = model -> dry_mass_kg + model -> propellant_mass_kg;
  return SIM_OK;
}
