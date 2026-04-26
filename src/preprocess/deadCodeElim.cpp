#include "preprocess/deadCodeElim.h"

#include <queue>

using namespace SageInterface;

int DeadCodeEliminator::eliminate(SgNode *root)
{
    if (!root)
        return 0;

    std::vector<DefInfo> defs;
    std::unordered_map<SgVariableSymbol *, std::vector<SgStatement *>> defMap;
    std::unordered_set<SgStatement *> useful;

    collectDefinitions(root, defs, defMap);
    collectAbsolutelyUsefulStatements(root, useful);
    propagateUsefulness(root, defMap, useful);

    std::vector<SgStatement *> toRemove;

    for (const DefInfo &def : defs)
    {
        if (!def.stmt)
            continue;

        if (!def.removable)
            continue;

        if (useful.count(def.stmt))
            continue;

        toRemove.push_back(def.stmt);
    }

    int removed = 0;

    // 先收集再删除，避免遍历过程中 AST 失效
    for (SgStatement *stmt : toRemove)
    {
        if (!stmt)
            continue;

        SageInterface::removeStatement(stmt);
        removed++;
    }

    return removed;
}

void DeadCodeEliminator::collectDefinitions(
    SgNode *root,
    std::vector<DefInfo> &defs,
    std::unordered_map<SgVariableSymbol *, std::vector<SgStatement *>> &defMap)
{
    Rose_STL_Container<SgNode *> stmts =
        NodeQuery::querySubTree(root, V_SgStatement);

    for (SgNode *n : stmts)
    {
        SgStatement *stmt = isSgStatement(n);
        if (!stmt)
            continue;

        if (!isRemovableDefinitionStatement(stmt))
            continue;

        SgVariableSymbol *sym = getDefinedVariable(stmt);
        if (!sym)
            continue;

        DefInfo info;
        info.stmt = stmt;
        info.symbol = sym;
        info.removable = true;

        defs.push_back(info);
        defMap[sym].push_back(stmt);
    }
}

void DeadCodeEliminator::collectAbsolutelyUsefulStatements(
    SgNode *root,
    std::unordered_set<SgStatement *> &useful)
{
    Rose_STL_Container<SgNode *> stmts =
        NodeQuery::querySubTree(root, V_SgStatement);

    for (SgNode *n : stmts)
    {
        SgStatement *stmt = isSgStatement(n);
        if (!stmt)
            continue;

        if (isAbsolutelyUsefulStatement(stmt))
            useful.insert(stmt);
    }
}

void DeadCodeEliminator::propagateUsefulness(
    SgNode *root,
    const std::unordered_map<SgVariableSymbol *, std::vector<SgStatement *>> &defMap,
    std::unordered_set<SgStatement *> &useful)
{
    std::queue<SgStatement *> worklist;

    for (SgStatement *stmt : useful)
        worklist.push(stmt);

    while (!worklist.empty())
    {
        SgStatement *stmt = worklist.front();
        worklist.pop();

        std::unordered_set<SgVariableSymbol *> usedVars;
        collectUsedVariablesInStatement(stmt, usedVars);

        for (SgVariableSymbol *usedSym : usedVars)
        {
            if (!usedSym)
                continue;

            auto it = defMap.find(usedSym);
            if (it == defMap.end())
                continue;

            for (SgStatement *defStmt : it->second)
            {
                if (!defStmt)
                    continue;

                if (!isInsideRoot(root, defStmt))
                    continue;

                if (!useful.count(defStmt))
                {
                    useful.insert(defStmt);
                    worklist.push(defStmt);
                }
            }
        }
    }
}

void DeadCodeEliminator::collectUsedVariablesInStatement(
    SgStatement *stmt,
    std::unordered_set<SgVariableSymbol *> &usedVars)
{
    if (!stmt)
        return;

    Rose_STL_Container<SgNode *> refs =
        NodeQuery::querySubTree(stmt, V_SgVarRefExp);

    for (SgNode *n : refs)
    {
        SgVarRefExp *ref = isSgVarRefExp(n);
        if (!ref)
            continue;

        if (isVarRefDefinitionLHS(ref))
            continue;

        SgVariableSymbol *sym = ref->get_symbol();
        if (sym)
            usedVars.insert(sym);
    }
}

bool DeadCodeEliminator::isAbsolutelyUsefulStatement(SgStatement *stmt)
{
    if (!stmt)
        return false;

    // 控制流语句保守保留
    if (isSgForStatement(stmt) ||
        isSgWhileStmt(stmt) ||
        isSgDoWhileStmt(stmt) ||
        isSgIfStmt(stmt) ||
        isSgSwitchStatement(stmt) ||
        isSgCaseOptionStmt(stmt) ||
        isSgDefaultOptionStmt(stmt) ||
        isSgBreakStmt(stmt) ||
        isSgContinueStmt(stmt) ||
        isSgGotoStatement(stmt) ||
        isSgLabelStatement(stmt) ||
        isSgReturnStmt(stmt))
    {
        return true;
    }

    // 函数调用保守认为有副作用
    Rose_STL_Container<SgNode *> calls =
        NodeQuery::querySubTree(stmt, V_SgFunctionCallExp);
    if (!calls.empty())
        return true;

    // 数组写、指针写、结构体成员写等保守认为有用
    if (auto exprStmt = isSgExprStatement(stmt))
    {
        SgExpression *expr = exprStmt->get_expression();

        if (auto assign = isSgAssignOp(expr))
        {
            SgExpression *lhs = assign->get_lhs_operand();

            if (isSgPntrArrRefExp(lhs) ||
                isSgPointerDerefExp(lhs) ||
                isSgDotExp(lhs) ||
                isSgArrowExp(lhs))
            {
                return true;
            }
        }

        if (expressionHasSideEffect(expr))
            return true;
    }

    return false;
}

bool DeadCodeEliminator::isRemovableDefinitionStatement(SgStatement *stmt)
{
    if (!stmt)
        return false;

    // int x = expr;
    if (auto varDecl = isSgVariableDeclaration(stmt))
    {
        // 只处理单变量声明，避免 int a,b; 删除困难
        if (varDecl->get_variables().size() != 1)
            return false;

        SgInitializedName *initName = varDecl->get_variables()[0];
        if (!initName)
            return false;

        SgInitializer *init = initName->get_initializer();

        // 没有初始化的声明可以删
        if (!init)
            return true;

        // 初始化表达式有副作用，不能删
        if (auto assignInit = isSgAssignInitializer(init))
        {
            SgExpression *rhs = assignInit->get_operand_i();
            if (expressionHasSideEffect(rhs))
                return false;
        }
        else
        {
            // 复杂 initializer 保守保留
            return false;
        }

        return true;
    }

    // x = expr;
    if (auto exprStmt = isSgExprStatement(stmt))
    {
        auto assign = isSgAssignOp(exprStmt->get_expression());
        if (!assign)
            return false;

        SgExpression *lhs = assign->get_lhs_operand();
        SgExpression *rhs = assign->get_rhs_operand();

        // 只删除普通标量变量赋值
        if (!isSgVarRefExp(lhs))
            return false;

        // 右值有副作用，不能删
        if (expressionHasSideEffect(rhs))
            return false;

        return true;
    }

    return false;
}

SgVariableSymbol *DeadCodeEliminator::getDefinedVariable(SgStatement *stmt)
{
    if (!stmt)
        return nullptr;

    if (auto varDecl = isSgVariableDeclaration(stmt))
    {
        if (varDecl->get_variables().size() != 1)
            return nullptr;

        SgInitializedName *initName = varDecl->get_variables()[0];
        if (!initName)
            return nullptr;

        return isSgVariableSymbol(initName->search_for_symbol_from_symbol_table());
    }

    if (auto exprStmt = isSgExprStatement(stmt))
    {
        if (auto assign = isSgAssignOp(exprStmt->get_expression()))
        {
            if (auto lhs = isSgVarRefExp(assign->get_lhs_operand()))
                return lhs->get_symbol();
        }
    }

    return nullptr;
}

bool DeadCodeEliminator::expressionHasSideEffect(SgExpression *expr)
{
    if (!expr)
        return false;

    if (isSgFunctionCallExp(expr))
        return true;

    if (isSgAssignOp(expr) ||
        isSgPlusAssignOp(expr) ||
        isSgMinusAssignOp(expr) ||
        isSgMultAssignOp(expr) ||
        isSgDivAssignOp(expr) ||
        isSgModAssignOp(expr) ||
        isSgAndAssignOp(expr) ||
        isSgIorAssignOp(expr) ||
        isSgXorAssignOp(expr) ||
        isSgLshiftAssignOp(expr) ||
        isSgRshiftAssignOp(expr) ||
        isSgPlusPlusOp(expr) ||
        isSgMinusMinusOp(expr))
    {
        return true;
    }

    Rose_STL_Container<SgNode *> calls =
        NodeQuery::querySubTree(expr, V_SgFunctionCallExp);
    if (!calls.empty())
        return true;

    Rose_STL_Container<SgNode *> assigns =
        NodeQuery::querySubTree(expr, V_SgAssignOp);
    if (!assigns.empty())
        return true;

    Rose_STL_Container<SgNode *> pp =
        NodeQuery::querySubTree(expr, V_SgPlusPlusOp);
    if (!pp.empty())
        return true;

    Rose_STL_Container<SgNode *> mm =
        NodeQuery::querySubTree(expr, V_SgMinusMinusOp);
    if (!mm.empty())
        return true;

    return false;
}

bool DeadCodeEliminator::isVarRefDefinitionLHS(SgVarRefExp *ref)
{
    if (!ref)
        return false;

    SgNode *parent = ref->get_parent();

    if (auto assign = isSgAssignOp(parent))
        return assign->get_lhs_operand() == ref;

    if (auto op = isSgPlusAssignOp(parent))
        return op->get_lhs_operand() == ref;

    if (auto op = isSgMinusAssignOp(parent))
        return op->get_lhs_operand() == ref;

    if (auto op = isSgMultAssignOp(parent))
        return op->get_lhs_operand() == ref;

    if (auto op = isSgDivAssignOp(parent))
        return op->get_lhs_operand() == ref;

    if (auto op = isSgModAssignOp(parent))
        return op->get_lhs_operand() == ref;

    if (auto op = isSgPlusPlusOp(parent))
        return op->get_operand() == ref;

    if (auto op = isSgMinusMinusOp(parent))
        return op->get_operand() == ref;

    return false;
}

bool DeadCodeEliminator::isInsideRoot(SgNode *root, SgNode *node)
{
    if (!root || !node)
        return false;

    SgNode *cur = node;
    while (cur)
    {
        if (cur == root)
            return true;
        cur = cur->get_parent();
    }

    return false;
}