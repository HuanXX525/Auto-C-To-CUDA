#include <rose.h>
#include <staticSingleAssignment.h>
#include <reachingDef.h>

#include "preprocess/deadCodeElim.h"
#include "logger.h"
#include "DEBUG/debugTool.h"

#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <queue>

namespace {

// ---------------------------------------------------------------------------
//  Utility helpers
// ---------------------------------------------------------------------------

// Build a VarName for the SSA API
StaticSingleAssignment::VarName makeVarName(SgInitializedName* decl) {
    StaticSingleAssignment::VarName name;
    name.push_back(decl);
    return name;
}

// Flatten nested SgBasicBlocks into a linear list (program order),
// stopping at SgForStatement / SgWhileStmt boundaries.
void flattenStmts(SgBasicBlock* block, std::vector<SgStatement*>& out) {
    for (SgStatement* stmt : block->get_statements()) {
        if (isSgForStatement(stmt) || isSgWhileStmt(stmt) || isSgDoWhileStmt(stmt)) {
            out.push_back(stmt);
        } else if (SgBasicBlock* inner = isSgBasicBlock(stmt)) {
            flattenStmts(inner, out);
        } else {
            out.push_back(stmt);
        }
    }
}

// ---------- Side-effect / usefulness detection ----------

bool containsFunctionCall(SgStatement* stmt) {
    Rose_STL_Container<SgNode*> calls =
        NodeQuery::querySubTree(stmt, V_SgFunctionCallExp);
    return !calls.empty();
}

// Check if an expression node is an array-write on the LHS of an assignment.
// Walks up from the expression to find the enclosing assignment.
bool isArrayWriteLHS(SgNode* node) {
    if (!isSgPntrArrRefExp(node)) return false;
    // Walk up to find if this is on the LHS of an assignment
    SgNode* p = node->get_parent();
    while (p) {
        if (SgAssignOp* assign = isSgAssignOp(p))
            return assign->get_lhs_operand() == node;
        if (isSgExprStatement(p) || isSgBasicBlock(p) || isSgForStatement(p))
            break;
        p = p->get_parent();
    }
    return false;
}

// Check if a statement writes to any array (memory side effect)
bool hasArrayWrite(SgStatement* stmt) {
    Rose_STL_Container<SgNode*> refs =
        NodeQuery::querySubTree(stmt, V_SgPntrArrRefExp);
    for (auto it = refs.begin(); it != refs.end(); ++it)
        if (isArrayWriteLHS(*it)) return true;
    return false;
}

// 检测语句是否通过指针解引用写入内存 (*p = ...)。
// 内联函数常通过指针参数改写调用方内存（如 *p = value; ++p），
// hasArrayWrite 只覆盖 SgPntrArrRefExp（a[i]），漏掉了 *p 这类内存副作用。
// 若内联后仅靠 def-use 边保命，遇到循环自改变量的 phi 断链容易误删。
bool hasPointerDerefWrite(SgStatement* stmt) {
    Rose_STL_Container<SgNode*> derefs =
        NodeQuery::querySubTree(stmt, V_SgPointerDerefExp);
    for (auto it = derefs.begin(); it != derefs.end(); ++it) {
        SgNode* p = (*it)->get_parent();
        while (p) {
            if (SgAssignOp* assign = isSgAssignOp(p))
                return assign->get_lhs_operand() == (*it);
            if (isSgExprStatement(p) || isSgBasicBlock(p) ||
                isSgForStatement(p) || isSgWhileStmt(p))
                break;
            p = p->get_parent();
        }
    }
    return false;
}

// A statement is "absolutely useful" if it produces a visible effect
// or controls execution flow.
bool isUsefulStmt(SgStatement* stmt) {
    // Control flow statements
    if (isSgIfStmt(stmt)) return true;
    if (isSgForStatement(stmt)) return true;
    if (isSgWhileStmt(stmt)) return true;
    if (isSgDoWhileStmt(stmt)) return true;
    if (isSgGotoStatement(stmt)) return true;
    if (isSgSwitchStatement(stmt)) return true;
    if (isSgBreakStmt(stmt)) return true;
    if (isSgContinueStmt(stmt)) return true;
    if (isSgReturnStmt(stmt)) return true;

    // Side effects
    if (containsFunctionCall(stmt)) return true;
    if (hasArrayWrite(stmt)) return true;
    if (hasPointerDerefWrite(stmt)) return true;

    // Compound assignments and increments (e.g. a+=b, ++a) are accumulators
    // whose final value matters outside the loop. Treat as side effect.
    if (SgExprStatement* es = isSgExprStatement(stmt)) {
        SgExpression* e = es->get_expression();
        if (isSgCompoundAssignOp(e) || isSgPlusPlusOp(e) || isSgMinusMinusOp(e))
            return true;
    }

    return false;
}

// ---------- Variable definition extraction ----------

// Get the SgInitializedName defined by an assignment, compound assignment,
// increment/decrement, or variable declaration.
SgInitializedName* getDefinedVar(SgStatement* stmt) {
    SgExprStatement* es = isSgExprStatement(stmt);
    if (es) {
        SgExpression* expr = es->get_expression();
        if (SgAssignOp* assign = isSgAssignOp(expr)) {
            SgVarRefExp* lhs = isSgVarRefExp(assign->get_lhs_operand());
            if (lhs) return lhs->get_symbol()->get_declaration();
        }
        if (SgCompoundAssignOp* ca = isSgCompoundAssignOp(expr)) {
            SgVarRefExp* lhs = isSgVarRefExp(ca->get_lhs_operand());
            if (lhs) return lhs->get_symbol()->get_declaration();
        }
        if (isSgPlusPlusOp(expr) || isSgMinusMinusOp(expr)) {
            SgUnaryOp* uop = isSgUnaryOp(expr);
            SgVarRefExp* operand = isSgVarRefExp(uop->get_operand());
            if (operand) return operand->get_symbol()->get_declaration();
        }
        return nullptr;
    }
    SgVariableDeclaration* vd = isSgVariableDeclaration(stmt);
    if (vd) {
        SgInitializedNamePtrList& vars = vd->get_variables();
        if (vars.size() != 1) return nullptr;
        return vars[0];
    }
    return nullptr;
}

// Collect all variables defined by a statement (for building def-use map)
void collectDefs(SgStatement* stmt, std::vector<SgInitializedName*>& out) {
    if (SgInitializedName* v = getDefinedVar(stmt))
        out.push_back(v);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
//  Public entry point
// ---------------------------------------------------------------------------

void eliminateDeadCode(SgBasicBlock* block, StaticSingleAssignment *ssa_in) {
    if (!block) return;

    SgProject* project = SageInterface::getProject(block);
    if (!project) {
        log_error("Cannot find enclosing project for dead code elimination");
        return;
    }

    log_info("Running SSA-based dead code elimination");

    bool ownSsa = false;
    StaticSingleAssignment *ssa = ssa_in;
    if (!ssa) {
        fixForLoopTests(project);
        ssa = new StaticSingleAssignment(project);
        ownSsa = true;
        ssa->run(/*interprocedural=*/false, /*treatPointersAsStructures=*/false);
    }

    // Flatten all statements in the block into a linear list
    std::vector<SgStatement*> allStmts;
    flattenStmts(block, allStmts);

    if (allStmts.empty()) {
        if (ownSsa) delete ssa;
        return;
    }

    // ---------------------------------------------------------------
    //  Step 1: Build def-use edges using SSA reaching definitions
    // ---------------------------------------------------------------
    //  精确边（Bug fix #1, #2 — 来自 bfb2b99）：
    //    对每个 SgVarRefExp 查询 SSA 的精确实达定义，直接建边，
    //    避免了旧 defMap 的设计缺陷——同变量被重复定义时，后一次定义会
    //    覆盖前一次，导致前面的 uses 都错误地连到最后的 def。
    //    同时遍历 child 节点引用，解决了 getUsesAtNode(Stmt) 对数组
    //    下标等嵌套表达式返回空 uses 的问题。
    //
    //  保守回退（修复自 935c929 / 当前 bug）：
    //    被内联函数的参数副本声明用于循环内自改变量（如 while/for）时，
    //    SSA 在循环 header 处给出 phi 节点（合并了声明 def 与循环体内
    //    ++/-- 等多条 def）。若严格跳过 phi → 0 条边 → 声明在 Step 4
    //    被误删。另外循环体内的 def 不在 flattened allStmts 集合里，
    //    即使 SSA 报告的是单条非-phi reaching def，其 enclosingStmt
    //    也不在 stmtSet，同样导致 0 条边。
    //
    //    因此维护一个随程序顺序逐条推进的 lastDef 映射：
    //    - 当精确 reaching def 找到了可用的块内边时，使用精确边；
    //    - 当所有精确 def 都不可用（phi / null / 作用域外）时，
    //      回退到本块中程序顺序上离该引用最近的同变量定义。
    //    这样既保留了 SSA 精确边的优点（不误连到后出现的无关 def），
    //    又在循环自改变量场景下恢复了对前驱声明的保守连通。
    // ---------------------------------------------------------------

    // Fast lookup set for statements in this block
    std::set<SgStatement*> stmtSet(allStmts.begin(), allStmts.end());

    // 程序顺序上各变量的最近前驱定义（逐步更新，用于 phi/out-of-block 回退）
    std::map<StaticSingleAssignment::VarName, SgStatement*> lastDef;

    // Build predecessor edges: S → { defining statements of variables used in S }
    std::map<SgStatement*, std::set<SgStatement*>> preds;
    for (SgStatement* s : allStmts) {
        // Traverse all variable references within the statement
        Rose_STL_Container<SgNode*> varRefs =
            NodeQuery::querySubTree(s, V_SgVarRefExp);
        for (SgNode* n : varRefs) {
            SgVarRefExp* ref = isSgVarRefExp(n);
            if (!ref) continue;

            // Query precise reaching definitions at this use point
            const StaticSingleAssignment::NodeReachingDefTable& reachingDefs =
                ssa->getReachingDefsAtNode_(ref);

            for (const auto& rdEntry : reachingDefs) {
                const StaticSingleAssignment::ReachingDefPtr& reachingDef =
                    rdEntry.second;

                // Conservative: skip null reaching def
                if (!reachingDef) continue;

                // 跳过 phi 节点——多条定义汇合，不确定到底来自哪条
                if (reachingDef->isPhiFunction()) continue;

                // Get the AST node where this definition occurs
                SgNode* defNode = reachingDef->getDefinitionNode();
                if (!defNode) continue;

                // Find the enclosing statement of the definition
                SgStatement* defStmt =
                    SageInterface::getEnclosingNode<SgStatement>(defNode, true);
                if (!defStmt) continue;

                // Only build edges to statements within this block
                if (defStmt != s && stmtSet.count(defStmt)) {
                    preds[s].insert(defStmt);
                }
            }

            // 回退——被内联函数的参数副本声明用于循环内自改变量（如 while/for）时，
            // SSA 在循环 header 处给出 phi 节点，或循环体内的 def 不在 flattened
            // allStmts 集合内，导致本引用找不到任何可用达到定义。
            // 此时退回到程序顺序上离该引用最近的前驱同变量定义，恢复安全的保守连接。
            //
            // 注意：不能以 foundUsable（全局标记）为条件——getReachingDefsAtNode_
            // 返回的是该 AST 点所有变量的 reaching def 表，即便本变量的达到定义是 phi，
            // 其他变量（如 key、file）的条目仍可能标记 foundUsable=true 从而跳过
            // 对本变量的回退。因此改为无守卫地总是添加本变量的 lastDef 边，
            // preds 集合天然去重，不会引入多余边。
            {
                StaticSingleAssignment::VarName vn =
                    makeVarName(ref->get_symbol()->get_declaration());
                auto lit = lastDef.find(vn);
                if (lit != lastDef.end() && lit->second != s) {
                    preds[s].insert(lit->second);
                }
            }
        }

        // 将本语句的变量定义更新为最晚定义（lastDef 按程序顺序逐条推移）
        std::vector<SgInitializedName*> defs;
        collectDefs(s, defs);
        for (SgInitializedName* d : defs) {
            lastDef[makeVarName(d)] = s;
        }
    }

    // ---------------------------------------------------------------
    //  Step 2: Mark "absolutely useful" statements
    // ---------------------------------------------------------------
    std::set<SgStatement*> useful;
    std::queue<SgStatement*> worklist;

    for (SgStatement* s : allStmts) {
        if (isUsefulStmt(s)) {
            useful.insert(s);
            worklist.push(s);
        }
    }

    // ---------------------------------------------------------------
    //  Step 3: Backward propagation of usefulness via def-use edges
    // ---------------------------------------------------------------
    while (!worklist.empty()) {
        SgStatement* x = worklist.front();
        worklist.pop();

        // For each predecessor y of x (y defines a variable used by x)
        auto pit = preds.find(x);
        if (pit == preds.end()) continue;

        for (SgStatement* y : pit->second) {
            if (useful.find(y) == useful.end()) {
                useful.insert(y);
                worklist.push(y);
            }
        }
    }

    // ---------------------------------------------------------------
    //  Step 4: Delete all unmarked statements (in reverse order)
    // ---------------------------------------------------------------
    //  Also preserve the loop entry (init/test/increment statements)
    //  and the loop index variable declaration if inside the block.
    int removedCount = 0;
    for (auto it = allStmts.rbegin(); it != allStmts.rend(); ++it) {
        SgStatement* s = *it;
        if (useful.find(s) == useful.end() && getDefinedVar(s)) {
            SageInterface::removeStatement(s);
            removedCount++;
        }
    }

    if (removedCount > 0)
        log_info("Dead code elimination removed %d definitions", removedCount);

    if (ownSsa) delete ssa;
}
