/* Header file for induction variable analysis helpers */

#ifndef INDUCTION_VAR_ANALYSIS
#define INDUCTION_VAR_ANALYSIS

#include "rose.h"
#include "loop_attr.hpp"
#include "analysis/InductionVarInfo.hpp"

bool analyzeInductionVars(SgForStatement *loop_nest, InductionVarInfo &info);
bool isSimpleDerivedInductionExpr(SgExpression *expr, LoopNestAttribute *attr, SgInitializedName* &base_iter, int &scale, int &bias);

#endif
