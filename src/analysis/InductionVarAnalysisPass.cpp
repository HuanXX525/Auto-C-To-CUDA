/* Implementation of induction variable analysis helpers */

#include "analysis/InductionVarAnalysisPass.hpp"
#include "preprocess/preprocess.hpp"
#include "fileio/io.h"
#include "logger.h"
#include <algorithm>
#include <unordered_set>
#include <queue>
#include <vector>

namespace {

const std::unordered_set<std::string> &safeFunctionNameSet()
{
	static std::unordered_set<std::string> safe_names;
	static bool initialized = false;
	if(!initialized)
	{
		std::vector<std::string> names = Config::getInstance().getSafeFunctions();
		for(auto name_it = names.begin(); name_it != names.end(); name_it++)
			safe_names.insert(*name_it);
		initialized = true;
	}
	return safe_names;
}

struct AffineExprValue {
	bool valid;
	SgInitializedName *base_iter;
	int scale;
	int bias;

	AffineExprValue() : valid(false), base_iter(NULL), scale(0), bias(0) {}
	AffineExprValue(SgInitializedName *base, int s, int b) : valid(true), base_iter(base), scale(s), bias(b) {}
};

bool isSafeWhitelistedCall(SgFunctionCallExp *call)
{
	if(!call)
		return false;

	FuncAttribute *fa = dynamic_cast<FuncAttribute*>(call->getAttribute("FuncAttribute"));
	if(fa)
		return fa->isSafe();

	SgFunctionRefExp *fn_ref = isSgFunctionRefExp(call->get_function());
	if(!fn_ref)
		return false;

	std::string fn_name = fn_ref->get_symbol()->get_name().getString();
	return safeFunctionNameSet().find(fn_name) != safeFunctionNameSet().end();
}

bool expressionContainsUnsafeFunctionCall(SgExpression *expr)
{
	if(!expr)
		return false;

	Rose_STL_Container<SgNode*> calls = NodeQuery::querySubTree(expr, V_SgFunctionCallExp);
	for(auto call_it = calls.begin(); call_it != calls.end(); call_it++)
	{
		SgFunctionCallExp *call = isSgFunctionCallExp(*call_it);
		if(!isSafeWhitelistedCall(call))
			return true;
	}

	return false;
}

bool hasUnsafeControlOrPointerNodes(SgNode *root)
{
	if(!root)
		return true;

	if(!NodeQuery::querySubTree(root, V_SgIfStmt).empty())
		return true;
	if(!NodeQuery::querySubTree(root, V_SgWhileStmt).empty())
		return true;
	if(!NodeQuery::querySubTree(root, V_SgDoWhileStmt).empty())
		return true;
	if(!NodeQuery::querySubTree(root, V_SgSwitchStatement).empty())
		return true;
	if(!NodeQuery::querySubTree(root, V_SgAddressOfOp).empty())
		return true;
	if(!NodeQuery::querySubTree(root, V_SgPointerDerefExp).empty())
		return true;
	if(!NodeQuery::querySubTree(root, V_SgArrowExp).empty())
		return true;
	if(!NodeQuery::querySubTree(root, V_SgBreakStmt).empty())
		return true;
	if(!NodeQuery::querySubTree(root, V_SgContinueStmt).empty())
		return true;
	if(!NodeQuery::querySubTree(root, V_SgGotoStatement).empty())
		return true;

	return false;
}

bool containsUnsafeFunctionCalls(SgNode *root)
{
	Rose_STL_Container<SgNode*> calls = NodeQuery::querySubTree(root, V_SgFunctionCallExp);
	for(auto call_it = calls.begin(); call_it != calls.end(); call_it++)
	{
		SgFunctionCallExp *call = isSgFunctionCallExp(*call_it);
		if(!isSafeWhitelistedCall(call))
			return true;
	}
	return false;
}

bool containsFunctionCallInArrayIndices(SgNode *root)
{
	Rose_STL_Container<SgNode*> refs = NodeQuery::querySubTree(root, V_SgPntrArrRefExp);
	for(auto ref_it = refs.begin(); ref_it != refs.end(); ref_it++)
	{
		SgPntrArrRefExp *arr_ref = isSgPntrArrRefExp(*ref_it);
		if(!arr_ref)
			continue;

		std::vector<SgExpression*> *dim_info = new std::vector<SgExpression*>;
		SageInterface::isArrayReference(arr_ref, NULL, &dim_info);
		if(!dim_info)
			continue;

		for(auto dim_it = dim_info->begin(); dim_it != dim_info->end(); dim_it++)
		{
			if(!NodeQuery::querySubTree(*dim_it, V_SgFunctionCallExp).empty())
			{
				delete dim_info;
				return true;
			}
		}
		delete dim_info;
	}
	return false;
}

bool isEligibleLocalDecl(SgInitializedName *decl, SgBasicBlock *block)
{
	if(!decl || !block)
		return false;

	if(!decl->get_type() || !decl->get_type()->isIntegerType())
		return false;

	if(decl->get_scope() != block)
		return false;

	if(isSgGlobal(decl->get_scope()))
		return false;

	SgVariableDeclaration *var_decl = isSgVariableDeclaration(decl->get_declaration());
	if(!var_decl)
		return false;

	SgStorageModifier &storage = var_decl->get_declarationModifier().get_storageModifier();
	if(storage.isStatic() || storage.isExtern())
		return false;

	return true;
}

bool getIntConstValue(SgExpression *expr, int &value)
{
	if(!expr)
		return false;

	if(isSgIntVal(expr))
	{
		value = isSgIntVal(expr)->get_value();
		return true;
	}

	if(isSgMinusOp(expr))
	{
		int inner = 0;
		if(!getIntConstValue(isSgMinusOp(expr)->get_operand(), inner))
			return false;
		value = -inner;
		return true;
	}

	return false;
}

bool parseAffineExprWithEnv(
	SgExpression *expr,
	LoopNestAttribute *attr,
	const std::map<SgInitializedName*, AffineExprValue> &known_vars,
	AffineExprValue &out)
{
	if(!expr || !attr)
		return false;

	if(isSgIntVal(expr) || isSgMinusOp(expr))
	{
		int val = 0;
		if(!getIntConstValue(expr, val))
			return false;
		out = AffineExprValue(NULL, 0, val);
		return true;
	}

	if(isSgVarRefExp(expr))
	{
		SgInitializedName *decl = isSgVarRefExp(expr)->get_symbol()->get_declaration();
		if(attr->contains_iter_var(decl))
		{
			out = AffineExprValue(decl, 1, 0);
			return true;
		}

		auto known_it = known_vars.find(decl);
		if(known_it == known_vars.end() || !known_it->second.valid)
			return false;

		out = known_it->second;
		return true;
	}

	if(isSgAddOp(expr) || isSgSubtractOp(expr))
	{
		AffineExprValue lhs;
		AffineExprValue rhs;
		SgBinaryOp *op = isSgBinaryOp(expr);
		if(!parseAffineExprWithEnv(op->get_lhs_operand(), attr, known_vars, lhs))
			return false;
		if(!parseAffineExprWithEnv(op->get_rhs_operand(), attr, known_vars, rhs))
			return false;

		if(lhs.base_iter && rhs.base_iter && lhs.base_iter != rhs.base_iter)
			return false;

		SgInitializedName *base_iter = lhs.base_iter ? lhs.base_iter : rhs.base_iter;
		int scale = 0;
		int bias = 0;
		if(isSgAddOp(expr))
		{
			scale = lhs.scale + rhs.scale;
			bias = lhs.bias + rhs.bias;
		}
		else
		{
			scale = lhs.scale - rhs.scale;
			bias = lhs.bias - rhs.bias;
		}

		out = AffineExprValue(base_iter, scale, bias);
		return true;
	}

	if(isSgMultiplyOp(expr))
	{
		SgExpression *lhs = isSgMultiplyOp(expr)->get_lhs_operand();
		SgExpression *rhs = isSgMultiplyOp(expr)->get_rhs_operand();
		int coeff = 0;

		if(getIntConstValue(lhs, coeff))
		{
			AffineExprValue value;
			if(!parseAffineExprWithEnv(rhs, attr, known_vars, value))
				return false;
			value.scale *= coeff;
			value.bias *= coeff;
			out = value;
			return true;
		}

		if(getIntConstValue(rhs, coeff))
		{
			AffineExprValue value;
			if(!parseAffineExprWithEnv(lhs, attr, known_vars, value))
				return false;
			value.scale *= coeff;
			value.bias *= coeff;
			out = value;
			return true;
		}
	}

	return false;
}

bool detectDerivedDefinition(SgStatement *stmt, SgBasicBlock *block, LoopNestAttribute *attr, SgInitializedName* &decl_out, SgExpression* &expr_out)
{
	decl_out = NULL;
	expr_out = NULL;

	if(!stmt || !block || !attr)
		return false;

	if(SgVariableDeclaration *var_decl = isSgVariableDeclaration(stmt))
	{
		SgInitializedNamePtrList &vars = var_decl->get_variables();
		if(vars.size() != 1)
			return false;

		SgInitializedName *decl = vars.front();
		if(!isEligibleLocalDecl(decl, block))
			return false;

		SgAssignInitializer *init = isSgAssignInitializer(decl->get_initializer());
		if(!init)
			return false;

		decl_out = decl;
		expr_out = init->get_operand();
		return true;
	}

	if(SgExprStatement *expr_stmt = isSgExprStatement(stmt))
	{
		SgAssignOp *assign = isSgAssignOp(expr_stmt->get_expression());
		if(!assign)
			return false;

		SgVarRefExp *lhs = isSgVarRefExp(assign->get_lhs_operand());
		if(!lhs)
			return false;

		SgInitializedName *decl = lhs->get_symbol()->get_declaration();
		if(!isEligibleLocalDecl(decl, block))
			return false;

		decl_out = decl;
		expr_out = assign->get_rhs_operand();
		return true;
	}

	return false;
}

bool isWrittenLater(SgInitializedName *decl, SgStatementPtrList &stmts, size_t start_idx)
{
	for(size_t idx = start_idx; idx < stmts.size(); idx++)
	{
		std::set<SgInitializedName*> reads, writes;
		SageInterface::collectReadWriteVariables(stmts[idx], reads, writes);
		if(writes.find(decl) != writes.end())
			return true;
	}

	return false;
}

bool collectDerivedDependencies(SgExpression *expr, const std::map<SgInitializedName*, AffineExprValue> &known_vars, std::set<SgInitializedName*> &deps_out)
{
	if(!expr)
		return true;

	Rose_STL_Container<SgNode*> refs = NodeQuery::querySubTree(expr, V_SgVarRefExp);
	for(auto ref_it = refs.begin(); ref_it != refs.end(); ref_it++)
	{
		SgVarRefExp *ref = isSgVarRefExp(*ref_it);
		if(!ref)
			continue;

		SgInitializedName *decl = ref->get_symbol()->get_declaration();
		auto known_it = known_vars.find(decl);
		if(known_it != known_vars.end())
			deps_out.insert(decl);
	}
	return true;
}

void topologicalSortDerived(InductionVarInfo &info)
{
	std::map<SgInitializedName*, int> indegree;
	for(auto dep_it = info.derived_dependencies.begin(); dep_it != info.derived_dependencies.end(); dep_it++)
	{
		SgInitializedName *decl = dep_it->first;
		indegree[decl] = 0;
	}

	for(auto dep_it = info.derived_dependencies.begin(); dep_it != info.derived_dependencies.end(); dep_it++)
	{
		for(auto edge_it = dep_it->second.begin(); edge_it != dep_it->second.end(); edge_it++)
		{
			if(indegree.find(*edge_it) == indegree.end())
				continue;
			indegree[dep_it->first]++;
		}
	}

	std::queue<SgInitializedName*> ready;
	for(auto in_it = indegree.begin(); in_it != indegree.end(); in_it++)
	{
		if(in_it->second == 0)
			ready.push(in_it->first);
	}

	std::vector<SgInitializedName*> order;
	while(!ready.empty())
	{
		SgInitializedName *curr = ready.front();
		ready.pop();
		order.push_back(curr);

		for(auto dep_it = info.derived_dependencies.begin(); dep_it != info.derived_dependencies.end(); dep_it++)
		{
			if(dep_it->second.find(curr) == dep_it->second.end())
				continue;
			indegree[dep_it->first]--;
			if(indegree[dep_it->first] == 0)
				ready.push(dep_it->first);
		}
	}

	/* Keep only acyclic, fully-resolved nodes. */
	if(order.size() == info.derived_dependencies.size())
		info.derived_order = order;
}

void collectBasicInductionVars(SgBasicBlock *block, InductionVarInfo &info)
{
	if(!block)
		return;

	SgStatementPtrList &stmts = block->get_statements();
	for(size_t idx = 0; idx < stmts.size(); idx++)
	{
		SgExprStatement *expr_stmt = isSgExprStatement(stmts[idx]);
		if(!expr_stmt)
			continue;

		SgExpression *expr = expr_stmt->get_expression();
		SgInitializedName *decl = NULL;

		if(SgPlusPlusOp *inc = isSgPlusPlusOp(expr))
		{
			if(SgVarRefExp *ref = isSgVarRefExp(inc->get_operand()))
				decl = ref->get_symbol()->get_declaration();
		}
		else if(SgMinusMinusOp *dec = isSgMinusMinusOp(expr))
		{
			if(SgVarRefExp *ref = isSgVarRefExp(dec->get_operand()))
				decl = ref->get_symbol()->get_declaration();
		}
		else if(SgPlusAssignOp *add_assign = isSgPlusAssignOp(expr))
		{
			int value = 0;
			if(isSgVarRefExp(add_assign->get_lhs_operand()) && getIntConstValue(add_assign->get_rhs_operand(), value))
				decl = isSgVarRefExp(add_assign->get_lhs_operand())->get_symbol()->get_declaration();
		}
		else if(SgMinusAssignOp *sub_assign = isSgMinusAssignOp(expr))
		{
			int value = 0;
			if(isSgVarRefExp(sub_assign->get_lhs_operand()) && getIntConstValue(sub_assign->get_rhs_operand(), value))
				decl = isSgVarRefExp(sub_assign->get_lhs_operand())->get_symbol()->get_declaration();
		}
		if(decl)
			info.bivs.insert(decl);
	}
}

bool analyzeBasicBlock(SgBasicBlock *block, LoopNestAttribute *attr, InductionVarInfo &info)
{
	if(!block || !attr)
		return false;

	std::map<SgInitializedName*, AffineExprValue> known_derived;
	SgStatementPtrList &stmts = block->get_statements();
	for(size_t idx = 0; idx < stmts.size(); idx++)
	{
		SgInitializedName *decl = NULL;
		SgExpression *expr = NULL;
		if(!detectDerivedDefinition(stmts[idx], block, attr, decl, expr))
			continue;
		if(expressionContainsUnsafeFunctionCall(expr))
			continue;

		AffineExprValue value;
		if(!parseAffineExprWithEnv(expr, attr, known_derived, value))
			continue;
		if(!value.base_iter || value.scale == 0)
			continue;
		if(isWrittenLater(decl, stmts, idx + 1))
			continue;

		std::set<SgInitializedName*> deps;
		collectDerivedDependencies(expr, known_derived, deps);

		info.exposable_divs.insert(decl);
		info.normalized_closed_form[decl] = DerivedAffineForm(value.base_iter, value.scale, value.bias);
		info.derived_dependencies[decl] = deps;
		info.definition_stmt[decl] = stmts[idx];
		info.definition_block[decl] = block;
		known_derived[decl] = value;
	}

	return !info.exposable_divs.empty();
}

} // namespace

bool isSimpleDerivedInductionExpr(SgExpression *expr, LoopNestAttribute *attr, SgInitializedName* &base_iter, int &scale, int &bias)
{
	std::map<SgInitializedName*, AffineExprValue> empty_env;
	AffineExprValue out;
	if(!parseAffineExprWithEnv(expr, attr, empty_env, out))
		return false;
	base_iter = out.base_iter;
	scale = out.scale;
	bias = out.bias;
	return (base_iter != NULL);
}

bool analyzeInductionVars(SgForStatement *loop_nest, InductionVarInfo &info)
{
	if(!loop_nest)
		return false;

	LoopNestAttribute *attr = dynamic_cast<LoopNestAttribute*>(loop_nest->getAttribute("LoopNestInfo"));
	if(!attr)
		return false;

	const std::vector<SgInitializedName*> &iter_vec = attr->get_iter_vec();
	info.bivs.insert(iter_vec.begin(), iter_vec.end());

	int loop_nest_size = attr->get_nest_size();
	Rose_STL_Container<SgNode*> inner_loops = NodeQuery::querySubTree(loop_nest, V_SgForStatement);
	SgBasicBlock *body = isSgBasicBlock(isSgForStatement(inner_loops[loop_nest_size - 1])->get_loop_body());
	if(!body)
		return false;

	if(hasUnsafeControlOrPointerNodes(body))
		return false;

	if(containsUnsafeFunctionCalls(body))
		return false;

	/* Keep index expressions purely affine for existing dependence tests. */
	if(containsFunctionCallInArrayIndices(body))
		return false;

	Rose_STL_Container<SgNode*> blocks = NodeQuery::querySubTree(body, V_SgBasicBlock);
	for(auto block_it = blocks.begin(); block_it != blocks.end(); block_it++)
	{
		SgBasicBlock *block = isSgBasicBlock(*block_it);
		if(!block)
			continue;
		analyzeBasicBlock(block, attr, info);
		collectBasicInductionVars(block, info);
	}

	topologicalSortDerived(info);
	if(info.derived_order.size() != info.exposable_divs.size())
	{
		/* Cyclic or incomplete dependency graph: conservatively disable exposure. */
		info.exposable_divs.clear();
		info.normalized_closed_form.clear();
		info.definition_stmt.clear();
		info.definition_block.clear();
		info.derived_dependencies.clear();
		info.derived_order.clear();
		return false;
	}

	if(!info.exposable_divs.empty())
		log_info("Induction analysis found %ld exposable derived variables", info.exposable_divs.size());

	return !info.exposable_divs.empty();
}
