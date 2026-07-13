#include "branch_flatten.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace branch_flatten {

static bool hasNestedIf(SgStatement *stmt)
{
    if (!stmt) return false;
    if (isSgIfStmt(stmt)) return true;

    Rose_STL_Container<SgNode *> ifNodes =
        NodeQuery::querySubTree(stmt, V_SgIfStmt);
    return !ifNodes.empty();
}

static bool isLeafIf(SgIfStmt *ifStmt)
{
    if (hasNestedIf(ifStmt->get_true_body())) return false;
    if (hasNestedIf(ifStmt->get_false_body())) return false;
    return true;
}

static std::vector<SgStatement *> bodyToList(SgStatement *body)
{
    if (!body) return {};
    if (auto *bb = isSgBasicBlock(body))
        return bb->get_statements();
    return {body};
}

static SgExpression *copyWithSubst(SgExpression                 *expr,
                                   const std::map<std::string, SgExpression *> &tempMap,
                                   std::set<std::string>        &visited)
{
    if (!expr) return nullptr;

    if (auto *vr = isSgVarRefExp(expr))
    {
        std::string name = vr->get_symbol()->get_name().getString();
        auto        it   = tempMap.find(name);
        if (it != tempMap.end() && it->second)
        {
            if (visited.count(name))
                return SageInterface::copyExpression(expr);
            visited.insert(name);
            auto *r = copyWithSubst(it->second, tempMap, visited);
            visited.erase(name);
            return r;
        }
        return SageInterface::copyExpression(expr);
    }

    if (auto *bin = isSgBinaryOp(expr))
    {
        std::set<std::string> v;
        auto                 *l = copyWithSubst(bin->get_lhs_operand(), tempMap, v);
        auto                 *r = copyWithSubst(bin->get_rhs_operand(), tempMap, v);

        if (isSgAddOp(expr))
            return SageBuilder::buildAddOp(l, r);
        if (isSgSubtractOp(expr))
            return SageBuilder::buildSubtractOp(l, r);
        if (isSgMultiplyOp(expr))
            return SageBuilder::buildMultiplyOp(l, r);
        if (isSgDivideOp(expr))
            return SageBuilder::buildDivideOp(l, r);
        if (isSgEqualityOp(expr))
            return SageBuilder::buildEqualityOp(l, r);
        if (isSgNotEqualOp(expr))
            return SageBuilder::buildNotEqualOp(l, r);
        if (isSgLessThanOp(expr))
            return SageBuilder::buildLessThanOp(l, r);
        if (isSgGreaterThanOp(expr))
            return SageBuilder::buildGreaterThanOp(l, r);
        if (isSgLessOrEqualOp(expr))
            return SageBuilder::buildLessOrEqualOp(l, r);
        if (isSgGreaterOrEqualOp(expr))
            return SageBuilder::buildGreaterOrEqualOp(l, r);
        if (isSgAndOp(expr))
            return SageBuilder::buildAndOp(l, r);
        if (isSgOrOp(expr))
            return SageBuilder::buildOrOp(l, r);
        if (isSgModOp(expr))
            return SageBuilder::buildModOp(l, r);
        if (isSgAssignOp(expr))
            return SageBuilder::buildAssignOp(l, r);

        return SageInterface::copyExpression(expr);
    }

    if (auto *un = isSgUnaryOp(expr))
    {
        std::set<std::string> v;
        auto                 *op = copyWithSubst(un->get_operand(), tempMap, v);

        if (isSgNotOp(expr))
            return SageBuilder::buildNotOp(op);
        if (isSgMinusOp(expr))
            return SageBuilder::buildMinusOp(op);
        if (isSgUnaryAddOp(expr))
            return SageBuilder::buildUnaryAddOp(op);

        return SageInterface::copyExpression(expr);
    }

    if (isSgCastExp(expr))
    {
        auto *cast = isSgCastExp(expr);
        std::set<std::string> v;
        auto *op = copyWithSubst(cast->get_operand(), tempMap, v);
        return SageBuilder::buildCastExp(op, cast->get_type());
    }

    return SageInterface::copyExpression(expr);
}

struct AfAssignment
{
    SgVarRefExp *lhs;
    SgExpression *rhs; // with temps substituted
};

static std::vector<AfAssignment>
extractAssignments(const std::vector<SgStatement *> &stmts)
{
    std::map<std::string, SgExpression *> tempMap;
    std::vector<AfAssignment>             result;

    for (auto *stmt : stmts)
    {
        if (auto *decl = isSgVariableDeclaration(stmt))
        {
            for (auto *init : decl->get_variables())
            {
                if (init->get_initializer())
                {
                    std::string name = init->get_name().getString();
                    tempMap[name] = init->get_initializer();
                }
            }
            continue;
        }

        auto *es = isSgExprStatement(stmt);
        if (!es) continue;
        auto *assign = isSgAssignOp(es->get_expression());
        if (!assign) continue;

        auto *lhs = assign->get_lhs_operand();
        auto *rhs = assign->get_rhs_operand();

        auto *lhsVar = isSgVarRefExp(lhs);
        if (!lhsVar) continue;

        std::string lhsName = lhsVar->get_symbol()->get_name().getString();

        if (tempMap.count(lhsName))
        {
            tempMap[lhsName] = SageInterface::copyExpression(rhs);
        }
        else
        {
            std::set<std::string> visited;
            SgExpression         *substRhs =
                copyWithSubst(rhs, tempMap, visited);
            result.push_back({isSgVarRefExp(SageInterface::copyExpression(lhs)),
                              substRhs});
        }
    }

    return result;
}

static SgExpression *buildAFExpr(SgExpression *cond, SgExpression *thenRhs,
                                 SgExpression *elseRhs)
{
    SgExpression *diff =
        SageBuilder::buildSubtractOp(SageInterface::copyExpression(thenRhs),
                                     SageInterface::copyExpression(elseRhs));
    SgExpression *prod =
        SageBuilder::buildMultiplyOp(SageInterface::copyExpression(cond), diff);
    return SageBuilder::buildAddOp(prod, SageInterface::copyExpression(elseRhs));
}

static SgExpression *buildAFNoElseExpr(SgExpression *cond,
                                       SgExpression *thenRhs,
                                       SgExpression *lhsVar)
{
    SgExpression *diff =
        SageBuilder::buildSubtractOp(SageInterface::copyExpression(thenRhs),
                                     SageInterface::copyExpression(lhsVar));
    SgExpression *prod =
        SageBuilder::buildMultiplyOp(SageInterface::copyExpression(cond), diff);
    return SageBuilder::buildAddOp(prod, SageInterface::copyExpression(lhsVar));
}

static int flattenIfStmt(SgIfStmt *ifStmt)
{
    SgStatement *condStmt = ifStmt->get_conditional();
    if (!condStmt) return 0;
    auto *condEs = isSgExprStatement(condStmt);
    if (!condEs) return 0;
    SgExpression *cond = condEs->get_expression();
    if (!cond) return 0;

    auto thenStmts = bodyToList(ifStmt->get_true_body());
    auto elseStmts = bodyToList(ifStmt->get_false_body());

    auto thenAssigns = extractAssignments(thenStmts);
    auto elseAssigns = extractAssignments(elseStmts);

    if (thenAssigns.empty()) return 0;

    std::map<std::string, SgExpression *> elseMap;
    for (auto &ea : elseAssigns)
        elseMap[ea.lhs->get_symbol()->get_name().getString()] = ea.rhs;

    std::vector<SgStatement *> newStmts;

    for (auto &ta : thenAssigns)
    {
        std::string lhsName = ta.lhs->get_symbol()->get_name().getString();
        SgExpression *afExpr = nullptr;

        auto eit = elseMap.find(lhsName);
        if (eit != elseMap.end())
        {
            afExpr = buildAFExpr(cond, ta.rhs, eit->second);
        }
        else
        {
            afExpr = buildAFNoElseExpr(cond, ta.rhs, ta.lhs);
        }

        if (!afExpr) continue;

        SgStatement *stmt =
            SageBuilder::buildExprStatement(
                SageBuilder::buildAssignOp(
                    SageInterface::copyExpression(ta.lhs), afExpr));
        newStmts.push_back(stmt);
    }

    if (newStmts.empty()) return 0;

    if (newStmts.size() == 1)
    {
        SageInterface::replaceStatement(ifStmt, newStmts[0], false);
    }
    else
    {
        SgBasicBlock *block = SageBuilder::buildBasicBlock();
        for (auto *s : newStmts)
            SageInterface::appendStatement(s, block);
        SageInterface::replaceStatement(ifStmt, block, false);
    }

    return 1;
}

static std::vector<SgIfStmt *> collectAllIfs(SgNode *root)
{
    std::vector<SgIfStmt *>          result;
    Rose_STL_Container<SgNode *> ifNodes =
        NodeQuery::querySubTree(root, V_SgIfStmt);
    for (auto *n : ifNodes)
    {
        if (auto *ifs = isSgIfStmt(n))
            result.push_back(ifs);
    }
    return result;
}

void flattenBranches(SgNode *root)
{
    if (!root) return;

    int flattened = 0;
    while (true)
    {
        auto allIfs = collectAllIfs(root);
        if (allIfs.empty()) break;

        SgIfStmt *target = nullptr;
        for (auto *ifs : allIfs)
        {
            if (isLeafIf(ifs))
            {
                target = ifs;
                break;
            }
        }
        if (!target) break;

        int n = flattenIfStmt(target);
        if (n == 0) break;
        flattened += n;
    }

    if (flattened > 0)
        AstPostProcessing(root);
}

} // namespace branch_flatten
