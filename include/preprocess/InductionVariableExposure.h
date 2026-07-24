#ifndef INDUCTIONVARIABLEEXPOSURE_H
#define INDUCTIONVARIABLEEXPOSURE_H
#include "rose.h"

class StaticSingleAssignment;

void inductionVariableExposure(SgForStatement *loop_nest, StaticSingleAssignment *ssa = nullptr);
#endif