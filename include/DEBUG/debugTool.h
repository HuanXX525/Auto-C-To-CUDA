#ifndef __DEBUG_TOOL__
#define __DEBUG_TOOL__
#include <rose.h>
#include <string>
#include <functional>

void checkParents(SgNode *root);
void checkParentConsistency(SgNode *root);
void checkVarRefs(SgNode *root);

void checkNullParent(SgNode *root, const std::string &tag,
                    std::function<void(const std::string&)> logger);

void fixForLoopTests(SgProject *project);
#endif

