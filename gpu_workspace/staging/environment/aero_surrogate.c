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
static int term_is_finite(const AeroSurrogateLinearTerm *term)
{
    return term != 0 &&
        isfinite(term->offset) &&
        isfinite(term->mach) &&
        isfinite(term->alpha_rad) &&
        isfinite(term->beta_rad);
}

/** @brief 计算一个线性项。 */
static double term_evaluate(
    const AeroSurrogateLinearTerm *term,
    double mach,
    double alpha_rad,
    double beta_rad)
{
    return term->offset +
        (term->mach * mach) +
        (term->alpha_rad * alpha_rad) +
        (term->beta_rad * beta_rad);
}

/** @brief 按名称选择模型项。 */
static AeroSurrogateLinearTerm *select_term(AeroSurrogateModel *model, const char *name)
{
    if (strcmp(name, "cx") == 0) {
        return &model->cx;
    }
    if (strcmp(name, "cy") == 0) {
        return &model->cy;
    }
    if (strcmp(name, "cz") == 0) {
        return &model->cz;
    }
    if (strcmp(name, "cl") == 0) {
        return &model->cl;
    }
    if (strcmp(name, "cm") == 0) {
        return &model->cm;
    }
    if (strcmp(name, "cn") == 0) {
        return &model->cn;
    }
    return 0;
}

SimStatus aero_surrogate_load_file(const char *path, AeroSurrogateModel *model)
{
    FILE *file;
    char magic[64];
    unsigned int seen_mask = 0u;
    unsigned int index;

    if (path == 0 || model == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    file = fopen(path, "rb");
    if (file == 0) {
        return SIM_ERR_IO;
    }
    if (fscanf(file, "%63s", magic) != 1 || strcmp(magic, AERO_SURROGATE_MAGIC) != 0) {
        (void)fclose(file);
        return SIM_ERR_CONFIG;
    }
    (void)memset(model, 0, sizeof(*model));
    for (index = 0u; index < 6u; ++index) {
        char name[8];
        AeroSurrogateLinearTerm term;
        AeroSurrogateLinearTerm *target;
        unsigned int bit;

        if (fscanf(
                file,
                "%7s %lf %lf %lf %lf",
                name,
                &term.offset,
                &term.mach,
                &term.alpha_rad,
                &term.beta_rad) != 5) {
            (void)fclose(file);
            return SIM_ERR_CONFIG;
        }
        target = select_term(model, name);
        if (target == 0 || !term_is_finite(&term)) {
            (void)fclose(file);
            return SIM_ERR_CONFIG;
        }
        bit = strcmp(name, "cx") == 0 ? 0x01u :
            strcmp(name, "cy") == 0 ? 0x02u :
            strcmp(name, "cz") == 0 ? 0x04u :
            strcmp(name, "cl") == 0 ? 0x08u :
            strcmp(name, "cm") == 0 ? 0x10u : 0x20u;
        if ((seen_mask & bit) != 0u) {
            (void)fclose(file);
            return SIM_ERR_CONFIG;
        }
        seen_mask |= bit;
        *target = term;
    }
    (void)fclose(file);
    return seen_mask == 0x3fu ? SIM_OK : SIM_ERR_CONFIG;
}

SimStatus aero_surrogate_evaluate(
    const AeroSurrogateModel *model,
    double mach,
    double alpha_rad,
    double beta_rad,
    AeroSurrogateCoefficients *out)
{
    if (model == 0 || out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!isfinite(mach) || mach < 0.0 || !isfinite(alpha_rad) || !isfinite(beta_rad) ||
        !term_is_finite(&model->cx) ||
        !term_is_finite(&model->cy) ||
        !term_is_finite(&model->cz) ||
        !term_is_finite(&model->cl) ||
        !term_is_finite(&model->cm) ||
        !term_is_finite(&model->cn)) {
        return SIM_ERR_NUMERIC;
    }
    out->cx = term_evaluate(&model->cx, mach, alpha_rad, beta_rad);
    out->cy = term_evaluate(&model->cy, mach, alpha_rad, beta_rad);
    out->cz = term_evaluate(&model->cz, mach, alpha_rad, beta_rad);
    out->cl = term_evaluate(&model->cl, mach, alpha_rad, beta_rad);
    out->cm = term_evaluate(&model->cm, mach, alpha_rad, beta_rad);
    out->cn = term_evaluate(&model->cn, mach, alpha_rad, beta_rad);
    if (!isfinite(out->cx) || !isfinite(out->cy) || !isfinite(out->cz) ||
        !isfinite(out->cl) || !isfinite(out->cm) || !isfinite(out->cn)) {
        return SIM_ERR_NUMERIC;
    }
    return SIM_OK;
}
