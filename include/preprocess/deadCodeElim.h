#ifndef DEAD_CODE_ELIMINATOR_H
#define DEAD_CODE_ELIMINATOR_H

#include <rose.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>

class DeadCodeEliminator
{
public:
    // 对任意 AST 子树执行轻量死代码消除
    // 常用传入：SgFunctionDefinition*、SgBasicBlock*、SgForStatement*
    // 返回删除的语句数量
    static int eliminate(SgNode *root);

private:
    struct DefInfo
    {
        SgStatement *stmt = nullptr;
        SgVariableSymbol *symbol = nullptr;
        bool removable = false;
    };

private:
    static void collectDefinitions(
        SgNode *root,
        std::vector<DefInfo> &defs,
        std::unordered_map<SgVariableSymbol *, std::vector<SgStatement *>> &defMap);

    static void collectAbsolutelyUsefulStatements(
        SgNode *root,
        std::unordered_set<SgStatement *> &useful);

    static void propagateUsefulness(
        SgNode *root,
        const std::unordered_map<SgVariableSymbol *, std::vector<SgStatement *>> &defMap,
        std::unordered_set<SgStatement *> &useful);

    static void collectUsedVariablesInStatement(
        SgStatement *stmt,
        std::unordered_set<SgVariableSymbol *> &usedVars);

    static bool isAbsolutelyUsefulStatement(SgStatement *stmt);

    static bool isRemovableDefinitionStatement(SgStatement *stmt);

    static SgVariableSymbol *getDefinedVariable(SgStatement *stmt);

    static bool expressionHasSideEffect(SgExpression *expr);

    static bool isVarRefDefinitionLHS(SgVarRefExp *ref);

    static bool isInsideRoot(SgNode *root, SgNode *node);
};

#endif