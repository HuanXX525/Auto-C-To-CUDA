#include <rose.h>
#include <staticSingleAssignment.h>

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

    return false;
}

// ---------- Variable definition extraction ----------

// Get the SgInitializedName defined by a simple assignment or decl.
SgInitializedName* getDefinedVar(SgStatement* stmt) {
    SgExprStatement* es = isSgExprStatement(stmt);
    if (es) {
        SgAssignOp* assign = isSgAssignOp(es->get_expression());
        if (!assign) return nullptr;
        SgVarRefExp* lhs = isSgVarRefExp(assign->get_lhs_operand());
        return lhs ? lhs->get_symbol()->get_declaration() : nullptr;
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
    //  Step 1: Build def-use edges using SSA
    // ---------------------------------------------------------------
    //  We maintain:  def_stmt_of[VarName] = the statement that defines it
    //  For each statement S, its def-use predecessors are:
    //    def_stmt_of[V] for each V used by S (from SSA getUsesAtNode)
    // ---------------------------------------------------------------

    // Build map: VarName → defining statement
    std::map<StaticSingleAssignment::VarName, SgStatement*> defMap;
    for (SgStatement* s : allStmts) {
        std::vector<SgInitializedName*> defs;
        collectDefs(s, defs);
        for (SgInitializedName* d : defs) {
            StaticSingleAssignment::VarName vn = makeVarName(d);
            defMap[vn] = s;
        }
    }

    // Build predecessor edges:  S → { predecessors }
    // predecessor of S = def_stmt_of[V] for each V used in S
    std::map<SgStatement*, std::set<SgStatement*>> preds;
    for (SgStatement* s : allStmts) {
        const StaticSingleAssignment::NodeReachingDefTable& uses =
            ssa->getUsesAtNode(s);
        for (const auto& useEntry : uses) {
            const StaticSingleAssignment::VarName& vn = useEntry.first;
            auto dit = defMap.find(vn);
            if (dit == defMap.end()) continue;

            SgStatement* defStmt = dit->second;
            if (defStmt != s)
                preds[s].insert(defStmt);
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
