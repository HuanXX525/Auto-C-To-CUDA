#include "transforms/PredicationPassTransformPass.hpp"
#include "analysis/PredicationAnalysis.hpp"
#include <rose.h>

using namespace c2cuda::analysis;

namespace c2cuda::transforms {

bool PredicationTransformPass::transform(SgProject* project, PassContext& ctx) {
    const auto& candidates =
        ctx.getRef<std::vector<PredicatableIf>>("predicatable_ifs");

    if (candidates.empty()) {
        log("Nothing to predicate.");
        return false;
    }

    int count = 0;
    for (auto& pred : candidates) {
        SgExpression* cond = pred.info->condition;
        if (!cond) continue;

        // 构建 cond ? thenRHS : elseRHS（或 cond ? thenRHS : lhs）
        SgExpression* falseExpr = pred.elseRHS
            ? SageInterface::copyExpression(pred.elseRHS)
            : SageInterface::copyExpression(pred.assignLHS);

        SgConditionalExp* ternary = SageBuilder::buildConditionalExp(
            SageInterface::copyExpression(cond),
            SageInterface::copyExpression(pred.thenRHS),
            falseExpr
        );

        SgExprStatement* newStmt = SageBuilder::buildExprStatement(
            SageBuilder::buildAssignOp(
                SageInterface::copyExpression(pred.assignLHS),
                ternary
            )
        );

        SageInterface::replaceStatement(pred.info->node, newStmt);
        count++;
    }

    log("Predicated " + std::to_string(count) + " if statements.");
    return count > 0;
}

} // namespace transforms
