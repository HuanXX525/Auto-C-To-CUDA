/* Header file for induction variable exposure helpers */

#ifndef INDUCTION_VAR_EXPOSE
#define INDUCTION_VAR_EXPOSE

#include "rose.h"
#include "analysis/InductionVarInfo.hpp"

bool exposeInductionVars(SgForStatement *loop_nest, const InductionVarInfo &info);
bool runInductionVarPass(SgForStatement *loop_nest);

#endif
