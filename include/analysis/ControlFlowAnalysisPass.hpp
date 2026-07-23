#pragma once

#include "pass/AnalysisPass.hpp"
#include "analysis/ControlFlowInfo.hpp"

namespace c2cuda::analysis {

class ControlFlowAnalysisPass : public AnalysisPass {
public:
    ControlFlowAnalysisPass()
        : AnalysisPass("ControlFlowAnalysis") {}

    void analyze(SgProject* project, PassContext& ctx) override;

private:

    // ============= if 分析 =============
    void analyzeIfs(SgFunctionDefinition* funcDef, FunctionCFGSummary& summary);

    // ============= loop 分析 =============
    void analyzeLoops(SgFunctionDefinition* funcDef, FunctionCFGSummary& summary);

    // ============= 辅助函数 =============

    int countStatements(SgStatement* stmt) {
        if (!stmt) return 0;
        if (SgBasicBlock* block = isSgBasicBlock(stmt))
            return block->get_statements().size();
        return 1;
    }

    int computeIfNesting(SgIfStmt* ifStmt) {
        int depth = 0;
        SgNode* parent = ifStmt->get_parent();
        while (parent) {
            if (isSgIfStmt(parent)) depth++;
            parent = parent->get_parent();
        }
        return depth;
    }

    int computeLoopNesting(SgForStatement* forStmt) {
        int depth = 0;
        SgNode* parent = forStmt->get_parent();
        while (parent) {
            if (isSgForStatement(parent) || isSgWhileStmt(parent)
                || isSgDoWhileStmt(parent))
                depth++;
            parent = parent->get_parent();
        }
        return depth;
    }

    bool containsNodeType(SgNode* root, VariantT variant) {
        Rose_STL_Container<SgNode*> nodes =
            NodeQuery::querySubTree(root, variant);
        // 排除 root 自身（避免 if 自己算入嵌套控制流）
        return nodes.size() > (isSgIfStmt(root) && variant == V_SgIfStmt ? 1 : 0);
    }

    bool containsNestedControlFlow(SgIfStmt* ifStmt) {
        // 检查 then/else 内部是否有嵌套的 if/for/while
        SgStatement* thenBody = ifStmt->get_true_body();
        SgStatement* elseBody = ifStmt->get_false_body();

        auto hasFlow = [](SgStatement* s) -> bool {
            if (!s) return false;
            Rose_STL_Container<SgNode*> ifs    = NodeQuery::querySubTree(s, V_SgIfStmt);
            Rose_STL_Container<SgNode*> fors   = NodeQuery::querySubTree(s, V_SgForStatement);
            Rose_STL_Container<SgNode*> whiles = NodeQuery::querySubTree(s, V_SgWhileStmt);
            return !ifs.empty() || !fors.empty() || !whiles.empty();
        };

        return hasFlow(thenBody) || hasFlow(elseBody);
    }

    bool checkSideEffects(SgIfStmt* ifStmt) {
        // 检查指针解引用写入、volatile 访问等
        Rose_STL_Container<SgNode*> ptrDeref =
            NodeQuery::querySubTree(ifStmt, V_SgPointerDerefExp);
        Rose_STL_Container<SgNode*> arrowExp =
            NodeQuery::querySubTree(ifStmt, V_SgArrowExp);
        // 保守策略：有指针操作就标记
        return !ptrDeref.empty() || !arrowExp.empty();
    }

    bool checkCanonicalLoop(SgForStatement* forStmt) {
        // 规范循环：for (int i = 0; i < N; i++)
        // 简化检测：有 init、有 cond、有 incr、无 break/goto
        SgForInitStatement* init = forStmt->get_for_init_stmt();
        if (!init || init->get_init_stmt().empty()) return false;
        if (!forStmt->get_test()) return false;
        if (!forStmt->get_increment()) return false;
        if (containsNodeType(forStmt->get_loop_body(), V_SgBreakStmt)) return false;
        if (containsNodeType(forStmt->get_loop_body(), V_SgGotoStatement)) return false;
        return true;
    }
};

} // namespace c2cuda
