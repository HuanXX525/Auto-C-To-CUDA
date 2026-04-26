#include "preprocess/exposeInductionVar.h"

using namespace SageInterface;
using namespace SageBuilder;

static std::string unparseKey(SgNode* n)
{
    return n ? n->unparseToString() : "";
}

static SgVariableSymbol* getForInductionVar(SgForStatement* forStmt)
{
    if (!forStmt) return nullptr;

    SgForInitStatement* init = forStmt->get_for_init_stmt();
    if (!init || init->get_init_stmt().empty()) return nullptr;

    SgStatement* s = init->get_init_stmt()[0];

    // 情况1：for (i = 1; ...)
    if (auto exprStmt = isSgExprStatement(s))
    {
        if (auto assign = isSgAssignOp(exprStmt->get_expression()))
        {
            if (auto lhs = isSgVarRefExp(assign->get_lhs_operand()))
                return lhs->get_symbol();
        }
    }

    // 情况2：for (int i = 1; ...)
    if (auto varDecl = isSgVariableDeclaration(s))
    {
        auto vars = varDecl->get_variables();
        if (!vars.empty())
        {
            SgInitializedName* name = vars[0];
            return isSgVariableSymbol(name->search_for_symbol_from_symbol_table());
        }
    }

    return nullptr;
}

static bool isSameVar(SgVarRefExp* ref, SgVariableSymbol* sym)
{
    return ref && sym && ref->get_symbol() == sym;
}

static bool isIntegerConstant(SgExpression* e)
{
    return isSgIntVal(e) ||
           isSgLongIntVal(e) ||
           isSgLongLongIntVal(e) ||
           isSgUnsignedIntVal(e) ||
           isSgUnsignedLongVal(e) ||
           isSgUnsignedLongLongIntVal(e);
}

// 判断表达式是否只由：归纳变量、常量、+、-、*常量 构成
// 例如：i、i-1、1*i+-1、2*i+3 都允许
static bool isAffineExprWrt(SgExpression* e, SgVariableSymbol* indVar)
{
    if (!e || !indVar) return false;

    // e = e->stripTypedefsAndModifiers();

    if (isIntegerConstant(e))
        return true;

    if (auto ref = isSgVarRefExp(e))
        return isSameVar(ref, indVar);

    if (auto minus = isSgMinusOp(e))
        return isAffineExprWrt(minus->get_operand(), indVar);

    if (auto add = isSgAddOp(e))
        return isAffineExprWrt(add->get_lhs_operand(), indVar) &&
               isAffineExprWrt(add->get_rhs_operand(), indVar);

    if (auto sub = isSgSubtractOp(e))
        return isAffineExprWrt(sub->get_lhs_operand(), indVar) &&
               isAffineExprWrt(sub->get_rhs_operand(), indVar);

    if (auto mul = isSgMultiplyOp(e))
    {
        SgExpression* lhs = mul->get_lhs_operand();
        SgExpression* rhs = mul->get_rhs_operand();

        // 只允许 常量 * affine 或 affine * 常量
        return (isIntegerConstant(lhs) && isAffineExprWrt(rhs, indVar)) ||
               (isIntegerConstant(rhs) && isAffineExprWrt(lhs, indVar));
    }

    return false;
}

struct TempDefInfo
{
    SgVariableSymbol* symbol = nullptr;
    SgExpression* rhs = nullptr;
    int defCount = 0;
    bool unsafe = false;
};

class CollectAffineTempDefs : public AstSimpleProcessing
{
public:
    SgForStatement* loop = nullptr;
    SgVariableSymbol* indVar = nullptr;
    std::unordered_map<SgVariableSymbol*, TempDefInfo> defs;

    CollectAffineTempDefs(SgForStatement* l, SgVariableSymbol* iv)
        : loop(l), indVar(iv) {}

    void visit(SgNode* node) override
    {
        if (!node) return;

        // 只处理当前 for 内部
        if (!isAncestor(loop, node))
            return;

        // 变量声明：int k = i - 1;
        if (auto varDecl = isSgVariableDeclaration(node))
        {
            for (auto initName : varDecl->get_variables())
            {
                auto sym = isSgVariableSymbol(initName->search_for_symbol_from_symbol_table());
                if (!sym) continue;

                auto init = initName->get_initializer();
                auto assignInit = isSgAssignInitializer(init);
                if (!assignInit) continue;

                SgExpression* rhs = assignInit->get_operand_i();
                if (!rhs) continue;

                auto& info = defs[sym];
                info.symbol = sym;
                info.defCount++;

                if (isAffineExprWrt(rhs, indVar))
                    info.rhs = rhs;
                else
                    info.unsafe = true;
            }
            return;
        }

        // 赋值语句：k = i - 1;
        if (auto assign = isSgAssignOp(node))
        {
            auto lhs = isSgVarRefExp(assign->get_lhs_operand());
            if (!lhs) return;

            SgVariableSymbol* sym = lhs->get_symbol();
            if (!sym) return;

            auto& info = defs[sym];
            info.symbol = sym;
            info.defCount++;

            SgExpression* rhs = assign->get_rhs_operand();
            if (isAffineExprWrt(rhs, indVar))
                info.rhs = rhs;
            else
                info.unsafe = true;

            return;
        }

        // 出现 ++k / k++ / --k / k--，认为不安全
        if (auto op = isSgPlusPlusOp(node))
        {
            if (auto ref = isSgVarRefExp(op->get_operand()))
                defs[ref->get_symbol()].unsafe = true;
        }

        if (auto op = isSgMinusMinusOp(node))
        {
            if (auto ref = isSgVarRefExp(op->get_operand()))
                defs[ref->get_symbol()].unsafe = true;
        }

        // &k 这种取地址，不做替换
        if (auto addr = isSgAddressOfOp(node))
        {
            if (auto ref = isSgVarRefExp(addr->get_operand()))
                defs[ref->get_symbol()].unsafe = true;
        }
    }
};

class SubstituteAffineTemps : public AstSimpleProcessing
{
public:
    SgForStatement* loop = nullptr;
    std::unordered_map<SgVariableSymbol*, SgExpression*> subst;
    int replaced = 0;

    SubstituteAffineTemps(
        SgForStatement* l,
        const std::unordered_map<SgVariableSymbol*, SgExpression*>& s)
        : loop(l), subst(s) {}

    bool isDefinitionLHS(SgVarRefExp* ref)
    {
        if (!ref) return false;

        SgNode* parent = ref->get_parent();

        if (auto assign = isSgAssignOp(parent))
            return assign->get_lhs_operand() == ref;

        if (auto initName = isSgInitializedName(parent))
            return true;

        if (auto pp = isSgPlusPlusOp(parent))
            return pp->get_operand() == ref;

        if (auto mm = isSgMinusMinusOp(parent))
            return mm->get_operand() == ref;

        return false;
    }

    void visit(SgNode* node) override
    {
        auto ref = isSgVarRefExp(node);
        if (!ref) return;

        if (!isAncestor(loop, ref))
            return;

        if (isDefinitionLHS(ref))
            return;

        SgVariableSymbol* sym = ref->get_symbol();
        auto it = subst.find(sym);
        if (it == subst.end())
            return;

        SgExpression* rhsCopy = isSgExpression(copyExpression(it->second));
        if (!rhsCopy) return;

        replaceExpression(ref, rhsCopy, true);
        replaced++;
    }
};

// 对单个 for 循环做归纳变量暴露
// 返回替换次数
int exposeInductionVariablesInFor(SgForStatement* forStmt)
{
    if (!forStmt) return 0;

    SgVariableSymbol* indVar = getForInductionVar(forStmt);
    if (!indVar) return 0;

    CollectAffineTempDefs collector(forStmt, indVar);
    collector.traverse(forStmt, preorder);

    std::unordered_map<SgVariableSymbol*, SgExpression*> subst;

    for (auto& kv : collector.defs)
    {
        SgVariableSymbol* sym = kv.first;
        TempDefInfo& info = kv.second;

        if (!sym) continue;
        if (sym == indVar) continue;
        if (info.unsafe) continue;
        if (info.defCount != 1) continue;
        if (!info.rhs) continue;

        subst[sym] = info.rhs;
    }

    if (subst.empty())
        return 0;

    SubstituteAffineTemps substituter(forStmt, subst);
    substituter.traverse(forStmt, preorder);

    return substituter.replaced;
}

