#include <rose.h>
#include "analysis/PredicationAnalysis.hpp"
#include "analysis/ControlFlowInfo.hpp"
#include "analysis/ControlFlowAnalysisPass.hpp"

namespace c2cuda::analysis {
void PredicationAnalysisPass::analyze(SgProject* /*project*/, PassContext& ctx) {
    // 直接从 context 取已有的控制流信息，不再遍历 AST
    const auto& cfInfo = ctx.getRef<ControlFlowInfo>("cfg_info");

    std::vector<PredicatableIf> candidates;

    for (auto* ifInfo : cfInfo.allIfs()) {
        PredicatableIf pred;
        if (canPredicate(ifInfo, pred)) {
            candidates.push_back(pred);
            log("Predicatable: line "
                + std::to_string(ifInfo->node->get_file_info()->get_line())
                + " in " + ifInfo->enclosingFunc
                    ->get_declaration()->get_name().getString());
        }
    }

    ctx.set<std::vector<PredicatableIf>>("predicatable_ifs", std::move(candidates));
    log("Total predicatable ifs: " + std::to_string(
        ctx.get<std::vector<PredicatableIf>>("predicatable_ifs").size()));
}


bool PredicationAnalysisPass::canPredicate(const IfInfo* info, PredicatableIf& out) {
    // 利用 cfg_info 中已计算好的属性做快速筛选
    // 条件 1：不能有嵌套控制流
    if (info->hasControlFlow) return false;

    // 条件 2：不能有函数调用（有副作用）
    if (info->hasFunctionCall) return false;

    // 条件 3：then 分支必须是单条赋值
    if (info->thenStmtCount != 1) return false;

    // 条件 4：有 else 的话，else 也必须是单条赋值
    if (info->elseBody && info->elseStmtCount != 1) return false;

    // 以下需要进一步检查 AST 细节
    SgAssignOp* thenAssign = extractAssignOp(info->thenBody);
    if (!thenAssign) return false;

    SgAssignOp* elseAssign = nullptr;
    if (info->elseBody) {
        elseAssign = extractAssignOp(info->elseBody);
        if (!elseAssign) return false;

        // 条件 5：then 和 else 必须赋值给同一左值
        if (thenAssign->get_lhs_operand()->unparseToString()
            != elseAssign->get_lhs_operand()->unparseToString())
            return false;
    }

    // 填充结果
    out.info     = info;
    out.assignLHS = thenAssign->get_lhs_operand();
    out.thenRHS   = thenAssign->get_rhs_operand();
    out.elseRHS   = elseAssign ? elseAssign->get_rhs_operand() : nullptr;
    return true;
}

SgAssignOp* PredicationAnalysisPass::extractAssignOp(SgStatement* stmt) {
    SgExprStatement* exprStmt = nullptr;
    if (auto* block = isSgBasicBlock(stmt)) {
        if (block->get_statements().size() != 1) return nullptr;
        exprStmt = isSgExprStatement(block->get_statements()[0]);
    } else {
        exprStmt = isSgExprStatement(stmt);
    }
    if (!exprStmt) return nullptr;
    return isSgAssignOp(exprStmt->get_expression());
}

}
