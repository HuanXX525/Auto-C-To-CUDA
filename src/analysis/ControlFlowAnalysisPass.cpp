#include <rose.h>
#include "pass/Pass.hpp"
#include "pass/AnalysisPass.hpp"
#include "analysis/ControlFlowInfo.hpp"
#include "sage3basic.h"
#include "analysis/ControlFlowAnalysisPass.hpp"

namespace c2cuda::analysis {


void ControlFlowAnalysisPass::analyze(SgProject* project, PassContext& ctx) {
    ControlFlowInfo cfInfo;

    // 遍历所有函数定义
    Rose_STL_Container<SgNode*> funcDefs =
        NodeQuery::querySubTree(project, V_SgFunctionDefinition);

    for (SgNode* node : funcDefs) {
        auto* funcDef = isSgFunctionDefinition(node);
        if (!funcDef) continue;

        FunctionCFGSummary summary;
        summary.funcDef = funcDef;
        summary.funcName = funcDef->get_declaration()->get_name().getString();

        analyzeIfs(funcDef, summary);
        analyzeLoops(funcDef, summary);

        summary.totalIfCount   = summary.ifs.size();
        summary.totalLoopCount = summary.loops.size();

        size_t idx = cfInfo.functions.size();
        cfInfo.funcIndex[funcDef] = idx;
        cfInfo.functions.push_back(std::move(summary));

        log("Function '" + cfInfo.functions[idx].funcName
            + "': " + std::to_string(cfInfo.functions[idx].totalIfCount) + " ifs, "
            + std::to_string(cfInfo.functions[idx].totalLoopCount) + " loops");
    }

    ctx.set<ControlFlowInfo>("cfg_info", std::move(cfInfo));
}

// ============= if 分析 =============
void ControlFlowAnalysisPass::analyzeIfs(SgFunctionDefinition* funcDef, FunctionCFGSummary& summary) {
    Rose_STL_Container<SgNode*> ifNodes =
        NodeQuery::querySubTree(funcDef, V_SgIfStmt);

    for (SgNode* n : ifNodes) {
        auto* ifStmt = isSgIfStmt(n);
        if (!ifStmt) continue;

        IfInfo info;
        info.node      = ifStmt;

        SgExprStatement* condStmt = isSgExprStatement(ifStmt->get_conditional());

        info.condition = ifStmt->get_conditional()
                         ? condStmt->get_expression()
                         : nullptr;

        info.thenBody  = ifStmt->get_true_body();
        info.elseBody  = ifStmt->get_false_body();

        info.thenStmtCount = countStatements(info.thenBody);
        info.elseStmtCount = info.elseBody ? countStatements(info.elseBody) : 0;
        info.nestingDepth  = computeIfNesting(ifStmt);

        info.hasFunctionCall = containsNodeType(ifStmt, V_SgFunctionCallExp);
        info.hasControlFlow  = containsNestedControlFlow(ifStmt);
        info.hasSideEffect   = checkSideEffects(ifStmt);

        info.enclosingLoop = SageInterface::getEnclosingNode<SgForStatement>(ifStmt, false);
        info.enclosingFunc = funcDef;

        if (info.nestingDepth > summary.maxIfDepth)
            summary.maxIfDepth = info.nestingDepth;

        summary.ifs.push_back(std::move(info));
    }
}

// ============= loop 分析 =============
void ControlFlowAnalysisPass::analyzeLoops(SgFunctionDefinition* funcDef, FunctionCFGSummary& summary) {
    Rose_STL_Container<SgNode*> forNodes =
        NodeQuery::querySubTree(funcDef, V_SgForStatement);

    for (SgNode* n : forNodes) {
        auto* forStmt = isSgForStatement(n);
        if (!forStmt) continue;

        LoopInfo info;
        info.node = forStmt;
        info.body = forStmt->get_loop_body();

        // 解析 init / cond / incr
        SgForInitStatement* initStmt = forStmt->get_for_init_stmt();
        if (initStmt && !initStmt->get_init_stmt().empty()) {
            SgExprStatement* es = isSgExprStatement(initStmt->get_init_stmt()[0]);
            if (es) info.initExpr = es->get_expression();
        }
        info.condExpr = forStmt->get_test_expr();
        info.incrExpr = forStmt->get_increment();

        info.bodyStmtCount = countStatements(info.body);
        info.nestingDepth  = computeLoopNesting(forStmt);

        // 循环体内的 if 数量
        Rose_STL_Container<SgNode*> innerIfs =
            NodeQuery::querySubTree(info.body, V_SgIfStmt);
        info.ifCount = innerIfs.size();

        info.hasBreak    = containsNodeType(info.body, V_SgBreakStmt);
        info.hasContinue = containsNodeType(info.body, V_SgContinueStmt);
        info.hasGoto     = containsNodeType(info.body, V_SgGotoStatement);

        // 规范循环检测（ROSE 提供了工具函数）
        info.isCanonical = checkCanonicalLoop(forStmt);

        info.enclosingFunc = funcDef;

        if (info.nestingDepth > summary.maxLoopDepth)
            summary.maxLoopDepth = info.nestingDepth;

        summary.loops.push_back(std::move(info));
    }
}


} // namespace analysis
