/** @file aero_surrogate.c
 *  @brief 只读线性气动代理模型实现。
 */
#include "env/aero_surrogate.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
/** @brief 代理模型文本格式标识。 */
#define AERO_SURROGATE_MAGIC "MISSILE_AERO_SURROGATE_LINEAR_V1"
/** @brief 校验线性项有限性。 */
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

static int term_is_finite(const AeroSurrogateLinearTerm *term)
{
  return term != 0 && __builtin_isfinite(term -> offset) && __builtin_isfinite(term -> mach) && __builtin_isfinite(term -> alpha_rad) && __builtin_isfinite(term -> beta_rad);
}
/** @brief 计算一个线性项。 */

static double term_evaluate(const AeroSurrogateLinearTerm *term,double mach,double alpha_rad,double beta_rad)
{
  return term -> offset + term -> mach * mach + term -> alpha_rad * alpha_rad + term -> beta_rad * beta_rad;
}
/** @brief 按名称选择模型项。 */

static AeroSurrogateLinearTerm *select_term(AeroSurrogateModel *model,const char *name)
{
  if (strcmp(name,"cx") == 0) {
    return &model -> cx;
  }
  if (strcmp(name,"cy") == 0) {
    return &model -> cy;
  }
  if (strcmp(name,"cz") == 0) {
    return &model -> cz;
  }
  if (strcmp(name,"cl") == 0) {
    return &model -> cl;
  }
  if (strcmp(name,"cm") == 0) {
    return &model -> cm;
  }
  if (strcmp(name,"cn") == 0) {
    return &model -> cn;
  }
  return 0;
}

SimStatus aero_surrogate_load_file(const char *path,AeroSurrogateModel *model)
{
  FILE *file;
  char magic[64];
  unsigned int seen_mask = 0u;
  unsigned int index;
  if (path == 0 || model == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  file = fopen(path,"rb");
  if (file == 0) {
    return SIM_ERR_IO;
  }
  if (fscanf(file,"%63s",magic) != 1 || strcmp(magic,"MISSILE_AERO_SURROGATE_LINEAR_V1") != 0) {
    (void )(fclose(file));
    return SIM_ERR_CONFIG;
  }
  (void )(memset(model,0,sizeof(( *model))));
  for (index = 0u; index < 6u; ++index) {
    char name[8];
    AeroSurrogateLinearTerm term;
    AeroSurrogateLinearTerm *target;
    unsigned int bit;
    if (fscanf(file,"%7s %lf %lf %lf %lf",name,&term . offset,&term . mach,&term . alpha_rad,&term . beta_rad) != 5) {
      (void )(fclose(file));
      return SIM_ERR_CONFIG;
    }
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
{
      AeroSurrogateModel *model__2_inline_1 = model;
      const char *name__3_inline_2 = name;
      if (strcmp(name__3_inline_2,"cx") == 0) {{
          target = &model__2_inline_1 -> cx;
          goto rose_inline_end__4;
        }
      }
      if (strcmp(name__3_inline_2,"cy") == 0) {{
          target = &model__2_inline_1 -> cy;
          goto rose_inline_end__4;
        }
      }
      if (strcmp(name__3_inline_2,"cz") == 0) {{
          target = &model__2_inline_1 -> cz;
          goto rose_inline_end__4;
        }
      }
      if (strcmp(name__3_inline_2,"cl") == 0) {{
          target = &model__2_inline_1 -> cl;
          goto rose_inline_end__4;
        }
      }
      if (strcmp(name__3_inline_2,"cm") == 0) {{
          target = &model__2_inline_1 -> cm;
          goto rose_inline_end__4;
        }
      }
      if (strcmp(name__3_inline_2,"cn") == 0) {{
          target = &model__2_inline_1 -> cn;
          goto rose_inline_end__4;
        }
      }
{
        target = 0;
        goto rose_inline_end__4;
      }
      rose_inline_end__4:
      ;
    }
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
{
      _Bool rose__temp2_inline_3 = 1;
      int rose_temp__9_inline_4;
      if (target == ((AeroSurrogateLinearTerm *)0)) {
        rose_temp__9_inline_4 = 1;
      }
       else {
        int rose_temp__10_inline_5;
{
          const AeroSurrogateLinearTerm *term__6_inline_6 = (&term);
{
            rose_temp__10_inline_5 = term__6_inline_6 != 0 && __builtin_isfinite(term__6_inline_6 -> offset) && __builtin_isfinite(term__6_inline_6 -> mach) && __builtin_isfinite(term__6_inline_6 -> alpha_rad) && __builtin_isfinite(term__6_inline_6 -> beta_rad);
            goto rose_inline_end__7;
          }
          rose_inline_end__7:
          ;
        }
        rose_temp__9_inline_4 = !rose_temp__10_inline_5;
      }
      rose__temp2_inline_3 = ((int )rose_temp__9_inline_4);
      if (rose__temp2_inline_3) {
        (void )(fclose(file));
        return SIM_ERR_CONFIG;
      }
    }
    bit = (strcmp(name,"cx") == 0?0x01u : ((strcmp(name,"cy") == 0?0x02u : ((strcmp(name,"cz") == 0?0x04u : ((strcmp(name,"cl") == 0?0x08u : ((strcmp(name,"cm") == 0?0x10u : 0x20u)))))))));
    if ((seen_mask & bit) != 0u) {
      (void )(fclose(file));
      return SIM_ERR_CONFIG;
    }
    seen_mask |= bit;
     *target = term;
  }
  (void )(fclose(file));
  return (seen_mask == 0x3fu?SIM_OK : SIM_ERR_CONFIG);
}

SimStatus aero_surrogate_evaluate(const AeroSurrogateModel *model,double mach,double alpha_rad,double beta_rad,AeroSurrogateCoefficients *out)
{
  if (model == 0 || out == 0) {
    return SIM_ERR_INVALID_ARG;
  }
  if (!__builtin_isfinite(mach) || mach < 0.0 || !__builtin_isfinite(alpha_rad) || !__builtin_isfinite(beta_rad) || !term_is_finite(&model -> cx) || !term_is_finite(&model -> cy) || !term_is_finite(&model -> cz) || !term_is_finite(&model -> cl) || !term_is_finite(&model -> cm) || !term_is_finite(&model -> cn)) {
    return SIM_ERR_NUMERIC;
  }
  out -> cx = term_evaluate(&model -> cx,mach,alpha_rad,beta_rad);
  out -> cy = term_evaluate(&model -> cy,mach,alpha_rad,beta_rad);
  out -> cz = term_evaluate(&model -> cz,mach,alpha_rad,beta_rad);
  out -> cl = term_evaluate(&model -> cl,mach,alpha_rad,beta_rad);
  out -> cm = term_evaluate(&model -> cm,mach,alpha_rad,beta_rad);
  out -> cn = term_evaluate(&model -> cn,mach,alpha_rad,beta_rad);
  if (!__builtin_isfinite(out -> cx) || !__builtin_isfinite(out -> cy) || !__builtin_isfinite(out -> cz) || !__builtin_isfinite(out -> cl) || !__builtin_isfinite(out -> cm) || !__builtin_isfinite(out -> cn)) {
    return SIM_ERR_NUMERIC;
  }
  return SIM_OK;
}
