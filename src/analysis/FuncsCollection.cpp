#include <rose.h>
#include "sage3basic.h"
#include "analysis/FuncCollection.hpp"
#include <string>
#include <utility>
#include <vector>

namespace c2cuda::analysis {

// Collect all functions and count.
void FuncsCollectPass::analyze(SgProject* project, c2cuda::PassContext& ctx) {
    std::vector<SgFunctionDeclaration*> funcs;

    Rose_STL_Container<SgNode*> nodes = NodeQuery::querySubTree(project, V_SgFunctionDeclaration);

    for(SgNode* n : nodes) {
        if(auto * funcDecl = isSgFunctionDeclaration(n)) {
            if(funcDecl->get_definingDeclaration() == funcDecl) {
                funcs.push_back(funcDecl);
                log("Found function: " + funcDecl->get_name().getString());
            }
        }
    }

    log("Total functions: " + std::to_string(funcs.size()));
    ctx.set<std::vector<SgFunctionDeclaration*>>("functions", std::move(funcs));
}

} // namespace transforms
