#include <rose.h>
#include <analysis/LoopCollection.hpp>

namespace c2cuda::analysis {

void CollectForLoopsPass::analyze(SgProject* project, c2cuda::PassContext& ctx) {
    std::vector<SgForStatement*> loops;

    Rose_STL_Container<SgNode*> nodes =
        NodeQuery::querySubTree(project, V_SgForStatement);

    for (SgNode* n : nodes) {
        if (auto* forStmt = isSgForStatement(n)) {
            loops.push_back(forStmt);
            log("Found for-loop at line " +
                std::to_string(forStmt->get_file_info()->get_line()));
        }
    }

    log("Total for-loops: " + std::to_string(loops.size()));
    // 存入上下文，供后续 pass 使用
    ctx.set<std::vector<SgForStatement*>>("for_loops", std::move(loops));
}

} // namespace transforms
