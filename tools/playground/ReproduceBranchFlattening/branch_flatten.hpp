#pragma once
#include <rose.h>

namespace branch_flatten {

/**
 * @brief Flatten all branch statements (if/if-else/else-if) inside @p root
 *        into branch-free arithmetic expressions using Algorithm Flattening.
 *
 * @param root  A ROSE AST node (e.g. SgFunctionDefinition, SgBasicBlock).
 *              The function is NOT recursive — it operates on statements
 *              directly contained in @p root.
 */
void flattenBranches(SgNode *root);

} // namespace branch_flatten
