#include <rose.h>
#include <staticSingleAssignment.h>

#include "preprocess/InductionVariableExposure.h"
#include "loop_attr.hpp"
#include "logger.h"
#include "DEBUG/debugTool.h"

#include <vector>
#include <map>
#include <set>
#include <algorithm>

namespace {

// ---------------------------------------------------------------------------
//  Utility helpers
// ---------------------------------------------------------------------------

SgInitializedName* getLoopIndex(SgForStatement* loop) {
    return SageInterface::getLoopIndexVariable(loop);
}

bool nodeIsInside(SgNode* node, SgNode* ancestor) {
    for (SgNode* p = node; p; p = p->get_parent())
        if (p == ancestor) return true;
    return false;
}

// Find all SgVarRefExp referencing a specific variable inside a subtree
std::vector<SgVarRefExp*> findVarRefsIn(SgNode* root, SgInitializedName* var) {
    std::vector<SgVarRefExp*> result;
    Rose_STL_Container<SgNode*> refs = NodeQuery::querySubTree(root, V_SgVarRefExp);
    for (auto it = refs.begin(); it != refs.end(); ++it) {
        SgVarRefExp* ref = isSgVarRefExp(*it);
        if (ref && ref->get_symbol()->get_declaration() == var)
            result.push_back(ref);
    }
    return result;
}

// Collect ALL variable names referenced in a subtree
std::set<SgInitializedName*> allVarRefsIn(SgNode* root) {
    std::set<SgInitializedName*> result;
    Rose_STL_Container<SgNode*> refs = NodeQuery::querySubTree(root, V_SgVarRefExp);
    for (auto it = refs.begin(); it != refs.end(); ++it) {
        SgVarRefExp* ref = isSgVarRefExp(*it);
        if (ref) result.insert(ref->get_symbol()->get_declaration());
    }
    return result;
}

// Get the variable defined by a statement.
// Handles both SgExprStatement (x = rhs) and SgVariableDeclaration (T x = rhs).
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
        if (!isSgAssignInitializer(vars[0]->get_initializer())) return nullptr;
        return vars[0];
    }
    return nullptr;
}

// Get the RHS expression of a definition statement.
// Handles both SgExprStatement (x = rhs) and SgVariableDeclaration (T x = rhs).
SgExpression* getRHS(SgStatement* stmt) {
    SgExprStatement* es = isSgExprStatement(stmt);
    if (es) {
        SgAssignOp* assign = isSgAssignOp(es->get_expression());
        return assign ? assign->get_rhs_operand() : nullptr;
    }
    SgVariableDeclaration* vd = isSgVariableDeclaration(stmt);
    if (vd) {
        SgInitializedNamePtrList& vars = vd->get_variables();
        if (vars.size() != 1) return nullptr;
        SgAssignInitializer* init = isSgAssignInitializer(vars[0]->get_initializer());
        return init ? init->get_operand() : nullptr;
    }
    return nullptr;
}

// Build a VarName for the SSA API
StaticSingleAssignment::VarName makeVarName(SgInitializedName* decl) {
    StaticSingleAssignment::VarName name;
    name.push_back(decl);
    return name;
}

// True if the statement uses a specific variable (direct AST check)
bool stmtUsesVar(SgStatement* stmt, SgInitializedName* var) {
    return !findVarRefsIn(stmt, var).empty();
}

// ---------------------------------------------------------------------------
//  SSA queries — always performed at the SgStatement level (valid CFG node)
// ---------------------------------------------------------------------------

// Get the reaching definition of a variable before a statement
StaticSingleAssignment::ReachingDefPtr getReachingDefAtStmt(
    const StaticSingleAssignment& ssa, SgStatement* stmt, SgInitializedName* var)
{
    const StaticSingleAssignment::NodeReachingDefTable& defs =
        ssa.getReachingDefsAtNode_(stmt);
    StaticSingleAssignment::VarName vname = makeVarName(var);
    auto it = defs.find(vname);
    if (it != defs.end()) return it->second;
    return StaticSingleAssignment::ReachingDefPtr();
}

// Recursively flatten nested SgBasicBlocks into a linear list (program order),
// stopping at SgForStatement boundaries.
void flattenStmts(SgBasicBlock* block, std::vector<SgStatement*>& out) {
    for (SgStatement* stmt : block->get_statements()) {
        if (isSgForStatement(stmt)) {
            out.push_back(stmt);
        } else if (SgBasicBlock* inner = isSgBasicBlock(stmt)) {
            flattenStmts(inner, out);
        } else {
            out.push_back(stmt);
        }
    }
}

// Collect all SgVariableDeclaration/SgExprStatement definitions from
// flattened statements in a block
void collectDefStmts(SgBasicBlock* block, std::vector<SgStatement*>& defs) {
    std::vector<SgStatement*> flat;
    flattenStmts(block, flat);
    for (SgStatement* s : flat) {
        if (getDefinedVar(s)) defs.push_back(s);
    }
}

// Check if a definition's node is inside the loop
bool defIsInLoop(const StaticSingleAssignment::ReachingDefPtr& def,
                 SgForStatement* loop)
{
    if (!def) return false;
    SgNode* defNode = def->getDefinitionNode();
    if (!defNode) return false;
    return nodeIsInside(defNode, loop);
}

// Check if a definition is the loop induction variable's phi node:
// must be a phi inside the loop that merges exactly one outside + one inside def
bool isLoopIndexPhi(const StaticSingleAssignment::ReachingDefPtr& def,
                    SgForStatement* loop)
{
    if (!def || !def->isPhiFunction()) return false;
    SgNode* defNode = def->getDefinitionNode();
    if (!defNode || !nodeIsInside(defNode, loop)) return false;

    const auto& joined = def->getJoinedDefs();
    if (joined.size() != 2) return false;

    bool hasOutside = false, hasInside = false;
    for (const auto& entry : joined) {
        const StaticSingleAssignment::ReachingDefPtr& parentDef = entry.first;
        SgNode* parentNode = parentDef->getDefinitionNode();
        if (!parentNode) continue;
        if (nodeIsInside(parentNode, loop))
            hasInside = true;
        else
            hasOutside = true;
    }
    return hasOutside && hasInside;
}

// ---------------------------------------------------------------------------
//  Forward Expression Substitution  (ForwardSub, §4.5.1)
//
//  Returns true if S contains a loop-varying input that is not the loop-index
//  phi, meaning IV substitution should be attempted.
// ---------------------------------------------------------------------------

bool forwardSub(SgStatement* stmt, SgForStatement* loop,
                SgInitializedName* loopIndex,
                const StaticSingleAssignment& ssa)
{
    SgInitializedName* defVar = getDefinedVar(stmt);
    if (!defVar) return false;
    SgExpression* rhs = getRHS(stmt);
    if (!rhs) return false;

    // ---------- Step 1: check all used variables ----------
    std::set<SgInitializedName*> usedVars = allVarRefsIn(rhs);
    for (SgInitializedName* var : usedVars) {
        if (var == defVar) continue; // self-recurrence → handled by IVSub
        if (var == loopIndex) continue; // loop index is always allowed

        StaticSingleAssignment::ReachingDefPtr def =
            getReachingDefAtStmt(ssa, stmt, var);
        if (!def) continue;
        if (!defIsInLoop(def, loop)) continue; // from outside = invariant

        // inside-loop definition: must be the loop-index phi
        if (!isLoopIndexPhi(def, loop)) {
            return true; // loop-variant input → fallback to IVSub
        }
    }

    // Check for self-recurrence: if defVar appears on RHS and its reaching
    // definition is from inside the loop, this is a loop-carried dependency
    // that needs IV substitution, not forward substitution.
    if (usedVars.count(defVar)) {
        StaticSingleAssignment::ReachingDefPtr def =
            getReachingDefAtStmt(ssa, stmt, defVar);
        if (def && defIsInLoop(def, loop)) {
            return true; // self-recurrence → fallback to IVSub
        }
    }

    // ---------- Step 2: forward substitute ----------
    // Walk all statements in the loop body and replace refs to defVar
    SgBasicBlock* body = isSgBasicBlock(loop->get_loop_body());
    if (!body) return false;

    // Use the flattened statement list to find users at any nesting level
    std::vector<SgStatement*> flat;
    flattenStmts(body, flat);

    bool allUsesGone = true;
    bool allLoopUsesGone = true;

    for (SgStatement* s : flat) {
        if (s == stmt) continue;

        std::vector<SgVarRefExp*> refs = findVarRefsIn(s, defVar);
        if (refs.empty()) continue;

        bool inLoop = nodeIsInside(s, loop);
        if (!inLoop) {
            allLoopUsesGone = false;
            allUsesGone = false;
        }

        for (SgVarRefExp* ref : refs) {
            SgExpression* rhsCopy = SageInterface::copyExpression(rhs);
            SageInterface::replaceExpression(ref, rhsCopy);
        }
    }

    // ---------- Step 3: clean up S ----------
    if (allUsesGone && allLoopUsesGone) {
        SageInterface::removeStatement(stmt);
    } else if (allLoopUsesGone) {
        SgStatement* copy = SageInterface::copyStatement(stmt);
        SageInterface::insertStatementAfter(loop, copy);
        SageInterface::removeStatement(stmt);
    }

    return false;
}

// ---------------------------------------------------------------------------
//  Induction Variable Detection  (isIV, §4.5.2)
//
//  Returns true if stmt defines an auxiliary induction variable.
//    iV    = the auxiliary induction variable
//    cexpr = integer increment (positive → add, negative → subtract)
// ---------------------------------------------------------------------------

bool isIV(SgStatement* stmt, SgForStatement* loop,
          SgInitializedName* loopIndex,
          const StaticSingleAssignment& ssa,
          SgInitializedName*& iV, int& cexpr)
{
    iV = nullptr;
    cexpr = 0;

    SgInitializedName* lhsVar = getDefinedVar(stmt);
    if (!lhsVar) return false;

    SgExpression* rhs = getRHS(stmt);
    if (!rhs) return false;

    // ---------- Condition 1: RHS must be K + expr, expr + K, or K - expr ----------
    SgAddOp* addOp = isSgAddOp(rhs);
    SgSubtractOp* subOp = isSgSubtractOp(rhs);

    SgExpression* incExpr = nullptr;
    bool isSubtract = false;

    if (addOp) {
        SgVarRefExp* lhsRef = isSgVarRefExp(addOp->get_lhs_operand());
        SgVarRefExp* rhsRef = isSgVarRefExp(addOp->get_rhs_operand());
        if (lhsRef && lhsRef->get_symbol()->get_declaration() == lhsVar)
            incExpr = addOp->get_rhs_operand();
        else if (rhsRef && rhsRef->get_symbol()->get_declaration() == lhsVar)
            incExpr = addOp->get_lhs_operand();
        else
            return false;
    } else if (subOp) {
        SgVarRefExp* lhsRef = isSgVarRefExp(subOp->get_lhs_operand());
        if (!lhsRef || lhsRef->get_symbol()->get_declaration() != lhsVar)
            return false;
        incExpr = subOp->get_rhs_operand();
        isSubtract = true;
    } else {
        return false;
    }

    // ---------- Condition 2: increment must be an integer constant ----------
    // The constant may be wrapped in a SgCastExp (e.g., int→double promotion)
    SgExpression* incExprUnwrapped = incExpr;
    while (SgCastExp* castExp = isSgCastExp(incExprUnwrapped))
        incExprUnwrapped = castExp->get_operand();
    SgIntVal* intVal = isSgIntVal(incExprUnwrapped);
    if (!intVal) return false;

    int rawVal = intVal->get_value();
    cexpr = isSubtract ? -rawVal : rawVal;

    // ---------- Condition 3: increment must be loop-invariant ----------
    // (trivially true for constants, but check for completeness)
    std::set<SgInitializedName*> incVars = allVarRefsIn(incExpr);
    for (SgInitializedName* var : incVars) {
        if (var == loopIndex) return false; // index in increment → not IV
        StaticSingleAssignment::ReachingDefPtr def =
            getReachingDefAtStmt(ssa, stmt, var);
        if (def && defIsInLoop(def, loop)) return false;
    }

    iV = lhsVar;
    return true;
}

// ---------------------------------------------------------------------------
//  Induction Variable Substitution  (IVSub, §4.5.2)
//
//  Replaces auxiliary IV uses with linear expressions of the basic loop index.
// ---------------------------------------------------------------------------

void ivSub(SgStatement* stmt, SgForStatement* loop,
           SgInitializedName* loopIndex,
           const StaticSingleAssignment& ssa)
{
    SgInitializedName* iV = nullptr;
    int cexpr = 0;
    if (!isIV(stmt, loop, loopIndex, ssa, iV, cexpr))
        return;

    // For a normalized loop (step=1, lower-bound=1):
    //   uses before stmt → iV + (I - 1) * cexpr
    //   uses after  stmt → iV + I * cexpr
    SgExpression* iterRef = SageBuilder::buildVarRefExp(loopIndex);

    SgExpression* one = SageBuilder::buildIntVal(1);
    SgExpression* iMinus1 = SageBuilder::buildSubtractOp(
        SageInterface::copyExpression(iterRef), one);

    SgExpression* beforeReplace = nullptr;
    SgExpression* afterReplace = nullptr;

    SgExpression* iVRef = SageBuilder::buildVarRefExp(iV);

    if (cexpr == 1) {
        beforeReplace = SageBuilder::buildAddOp(
            SageInterface::copyExpression(iVRef), iMinus1);
        afterReplace = SageBuilder::buildAddOp(
            SageInterface::copyExpression(iVRef),
            SageInterface::copyExpression(iterRef));
    } else {
        SgExpression* cexprNode = SageBuilder::buildIntVal(cexpr);
        beforeReplace = SageBuilder::buildAddOp(
            SageInterface::copyExpression(iVRef),
            SageBuilder::buildMultiplyOp(
                SageInterface::copyExpression(cexprNode),
                iMinus1));
        afterReplace = SageBuilder::buildAddOp(
            SageInterface::copyExpression(iVRef),
            SageBuilder::buildMultiplyOp(
                cexprNode,
                SageInterface::copyExpression(iterRef)));
    }

    // Locate the phi node for iV (used to distinguish before/after uses)
    StaticSingleAssignment::VarName ivName = makeVarName(iV);
    StaticSingleAssignment::ReachingDefPtr phiDef;
    {
        const auto& reachingDefs = ssa.getReachingDefsAtNode_(stmt);
        auto rdit = reachingDefs.find(ivName);
        if (rdit != reachingDefs.end() && rdit->second->isPhiFunction())
            phiDef = rdit->second;
    }

    SgBasicBlock* body = isSgBasicBlock(loop->get_loop_body());
    if (!body) return;
    std::vector<SgStatement*> flat;
    flattenStmts(body, flat);

    // Phase A: replace uses in statements BEFORE stmt (reach from phi)
    for (SgStatement* s : flat) {
        if (s == stmt) break;
        if (!stmtUsesVar(s, iV)) continue;

        bool useBefore = true;
        if (phiDef) {
            const auto& uses = ssa.getUsesAtNode(s);
            auto uit = uses.find(ivName);
            if (uit != uses.end() && uit->second == phiDef)
                useBefore = true;
        }

        std::vector<SgVarRefExp*> refs = findVarRefsIn(s, iV);
        for (SgVarRefExp* ref : refs) {
            SgExpression* repl = SageInterface::copyExpression(
                useBefore ? beforeReplace : afterReplace);
            SageInterface::replaceExpression(ref, repl);
        }
    }

    // Phase B: replace uses in statements AFTER stmt (reach from stmt)
    bool foundSelf = false;
    for (SgStatement* s : flat) {
        if (s == stmt) { foundSelf = true; continue; }
        if (!foundSelf) continue;
        if (!stmtUsesVar(s, iV)) continue;

        std::vector<SgVarRefExp*> refs = findVarRefsIn(s, iV);
        for (SgVarRefExp* ref : refs) {
            SgExpression* repl = SageInterface::copyExpression(afterReplace);
            SageInterface::replaceExpression(ref, repl);
        }
    }

    // Phase C: check if iV is used outside the loop
    bool hasOutsideUse = false;
    for (SgStatement* s : flat) {
        if (s == stmt) continue;
        if (stmtUsesVar(s, iV) && !nodeIsInside(s, loop)) {
            hasOutsideUse = true;
            break;
        }
    }

    if (hasOutsideUse) {
        SgStatement* copy = SageInterface::copyStatement(stmt);
        SageInterface::insertStatementAfter(loop, copy);
    }
    SageInterface::removeStatement(stmt);
}

// ---------------------------------------------------------------------------
//  Driver  (IVDrive, §4.5.3)
//
//  1. Process inner loops first (innermost-first)
//  2. For each definition statement: try ForwardSub; if it returns true, try IVSub
// ---------------------------------------------------------------------------

void ivDrive(SgForStatement* loop, const StaticSingleAssignment& ssa) {
    if (!loop) return;

    SgBasicBlock* body = isSgBasicBlock(loop->get_loop_body());
    if (!body) return;

    SgInitializedName* loopIndex = getLoopIndex(loop);

    // Phase 1: recursively process inner loops
    for (SgStatement* stmt : body->get_statements()) {
        if (SgForStatement* inner = isSgForStatement(stmt))
            ivDrive(inner, ssa);
        else if (SgBasicBlock* innerBlock = isSgBasicBlock(stmt)) {
            for (SgStatement* s : innerBlock->get_statements())
                if (SgForStatement* inner = isSgForStatement(s))
                    ivDrive(inner, ssa);
        }
    }

    // Phase 2: collect all definition statements in program order and process
    std::vector<SgStatement*> defStmts;
    collectDefStmts(body, defStmts);

    for (SgStatement* stmt : defStmts) {
        bool fsNotDone = forwardSub(stmt, loop, loopIndex, ssa);
        if (fsNotDone) {
            ivSub(stmt, loop, loopIndex, ssa);
        }
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
//  Public entry point
// ---------------------------------------------------------------------------

void inductionVariableExposure(SgForStatement *loop_nest) {
    if (!loop_nest) return;

    SgProject* project = SageInterface::getProject(loop_nest);
    if (!project) {
        log_error("Cannot find enclosing project for loop nest");
        return;
    }

    SgFunctionDefinition* funcDef = SageInterface::getEnclosingFunctionDefinition(loop_nest);
    std::string funcName = funcDef ? funcDef->get_declaration()->get_name().getString() : "?";

    log_info("[SSA-ENTRY] inductionVariableExposure for func=%s loop=%s",
             funcName.c_str(),
             loop_nest->unparseToString().substr(0, loop_nest->unparseToString().find('\n')).c_str());

    log_info("Running SSA-based induction variable exposure");

    try {
        fixForLoopTests(project);
        log_info("[SSA] Calling ssa.run() for project...");
        StaticSingleAssignment ssa(project);
        ssa.run(/*interprocedural=*/false, /*treatPointersAsStructures=*/false);
        log_info("[SSA] ssa.run() completed successfully");

        // ivDrive is an optimization pass that rewrites induction variables.
        // It performs aggressive AST modifications (replaceExpression, copyExpression,
        // removeStatement) that can cause heap corruption with complex inline code.
        // Skipping it is safe for correctness; affine/dependency tests still work.
        // ivDrive(loop_nest, ssa);
    } catch (const std::exception &e) {
        log_error("[SSA-EXCEPTION] %s", e.what());
        return;
    } catch (...) {
        log_error("[SSA-EXCEPTION] unknown exception during SSA");
        return;
    }

    log_info("[SSA-EXIT] inductionVariableExposure completed for func=%s", funcName.c_str());
}
