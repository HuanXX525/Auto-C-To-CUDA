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
	//
	// 关键：转换完成后修复受影响的子 AST，避免全项目 AstPostProcessing
	// 全项目 AstPostProcessing 会遍历所有函数，可能与先前的 loop
	// normalization/transformation 产生 for-loop 内部不一致（例如 test
	// expression 被错误地设置为 SgNullStatement），导致后续 SSA 断言失败。
	if (count > 0) {
		for (auto& pred : candidates) {
			SgNode *scope = pred.info->node->get_parent();
			if (scope) {
				SageInterface::fixVariableReferences(scope);
			}
		}
	}

    log("Predicated " + std::to_string(count) + " if statements.");
    return count > 0;
}

} // namespace transforms
