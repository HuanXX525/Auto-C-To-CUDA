#ifndef __DEBUG_TOOL__
#define __DEBUG_TOOL__
#include <rose.h>

void checkParents(SgNode *root);
void checkParentConsistency(SgNode *root);
void checkVarRefs(SgNode *root);
#endif

