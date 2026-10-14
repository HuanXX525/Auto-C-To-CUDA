/**
 * Device-only evaluation path derived from aero_surrogate.c.
 * File loading remains host-only in aero_surrogate.c.
 */
#include "env/aero_surrogate.h"
#include <math.h>
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

C2CUDA_HOST_DEVICE static int term_is_finite_device(const AeroSurrogateLinearTerm *term)
{
  return term != 0 && __builtin_isfinite(term -> offset) && __builtin_isfinite(term -> mach) && __builtin_isfinite(term -> alpha_rad) && __builtin_isfinite(term -> beta_rad);
}

C2CUDA_HOST_DEVICE static double term_evaluate_device(const AeroSurrogateLinearTerm *term,double mach,double alpha_rad,double beta_rad)
{
  return term -> offset + term -> mach * mach + term -> alpha_rad * alpha_rad + term -> beta_rad * beta_rad;
}

SimStatus aero_surrogate_evaluate(const AeroSurrogateModel *model,double mach,double alpha_rad,double beta_rad,AeroSurrogateCoefficients *out)
{
  if (model == 0 || out == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (!__builtin_isfinite(mach) || mach < 0.0 || !__builtin_isfinite(alpha_rad) || !__builtin_isfinite(beta_rad) || !term_is_finite_device(&model -> cx) || !term_is_finite_device(&model -> cy) || !term_is_finite_device(&model -> cz) || !term_is_finite_device(&model -> cl) || !term_is_finite_device(&model -> cm) || !term_is_finite_device(&model -> cn)) {
    return SIM_ERR_NUMERIC;
  }
  out -> cx = term_evaluate_device(&model -> cx,mach,alpha_rad,beta_rad);
  out -> cy = term_evaluate_device(&model -> cy,mach,alpha_rad,beta_rad);
  out -> cz = term_evaluate_device(&model -> cz,mach,alpha_rad,beta_rad);
  out -> cl = term_evaluate_device(&model -> cl,mach,alpha_rad,beta_rad);
  out -> cm = term_evaluate_device(&model -> cm,mach,alpha_rad,beta_rad);
  out -> cn = term_evaluate_device(&model -> cn,mach,alpha_rad,beta_rad);
  if (!__builtin_isfinite(out -> cx) || !__builtin_isfinite(out -> cy) || !__builtin_isfinite(out -> cz) || !__builtin_isfinite(out -> cl) || !__builtin_isfinite(out -> cm) || !__builtin_isfinite(out -> cn)) {
    return SIM_ERR_NUMERIC;
  }
  return SIM_OK;
}
