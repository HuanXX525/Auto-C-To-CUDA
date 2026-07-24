#ifndef DEADCODEELIM_H
#define DEADCODEELIM_H
#include "rose.h"

class StaticSingleAssignment;

void eliminateDeadCode(SgBasicBlock* block, StaticSingleAssignment *ssa = nullptr);
#endif
