/* Implementation of induction variable exposure helpers */

#include "transforms/InductionVarExposePass.hpp"
#include "analysis/InductionVarAnalysisPass.hpp"
#include "logger.h"

namespace {

SgExpression *buildClosedFormExpr(const DerivedAffineForm &form)
{
	if(!form.base_iter || form.scale == 0)
		return NULL;

	SgExpression *iter_ref = SageBuilder::buildVarRefExp(form.base_iter);
	SgExpression *linear_term = NULL;
	if(form.scale == 1)
		linear_term = iter_ref;
	else
		linear_term = SageBuilder::buildMultiplyOp(
			SageBuilder::buildIntVal(form.scale),
			iter_ref
		);

	if(form.bias == 0)
		return linear_term;

	return SageBuilder::buildAddOp(linear_term, SageBuilder::buildIntVal(form.bias));
}

void replaceRefsInStatement(SgStatement *stmt, const std::map<SgInitializedName*, DerivedAffineForm> &active_forms)
{
	if(!stmt || active_forms.empty())
		return;

	Rose_STL_Container<SgNode*> refs = NodeQuery::querySubTree(stmt, V_SgVarRefExp);
	std::vector<SgVarRefExp*> replace_list;
	for(auto ref_it = refs.begin(); ref_it != refs.end(); ref_it++)
	{
		SgVarRefExp *ref = isSgVarRefExp(*ref_it);
		if(!ref)
			continue;

		SgInitializedName *decl = ref->get_symbol()->get_declaration();
		if(active_forms.find(decl) != active_forms.end())
			replace_list.push_back(ref);
	}

	for(auto ref_it = replace_list.begin(); ref_it != replace_list.end(); ref_it++)
	{
		SgInitializedName *decl = (*ref_it)->get_symbol()->get_declaration();
		auto form_it = active_forms.find(decl);
		if(form_it == active_forms.end())
			continue;

		SgExpression *closed_form = buildClosedFormExpr(form_it->second);
		if(!closed_form)
			continue;

		SageInterface::replaceExpression(*ref_it, closed_form);
	}
}

bool exposeBasicBlock(const InductionVarInfo &info, SgBasicBlock *block)
{
	if(!block)
		return false;

	bool changed = false;
	std::map<SgInitializedName*, DerivedAffineForm> active_forms;
	SgStatementPtrList &stmts = block->get_statements();

	for(size_t idx = 0; idx < stmts.size(); idx++)
	{
		SgStatement *stmt = stmts[idx];

		/* Deliberately skip variable declarations to avoid enabling multi-layer chains. */
		if(!isSgVariableDeclaration(stmt))
		{
			replaceRefsInStatement(stmt, active_forms);
			if(!active_forms.empty())
				changed = true;
		}

		/* Activate derived variables in topological order once we pass their definition. */
		for(auto order_it = info.derived_order.begin(); order_it != info.derived_order.end(); order_it++)
		{
			SgInitializedName *decl = *order_it;
			auto block_it = info.definition_block.find(decl);
			auto stmt_it = info.definition_stmt.find(decl);
			auto form_it = info.normalized_closed_form.find(decl);
			if(block_it == info.definition_block.end() || stmt_it == info.definition_stmt.end() || form_it == info.normalized_closed_form.end())
				continue;

			if(block_it->second != block)
				continue;

			if(stmt_it->second == stmt)
				active_forms[decl] = form_it->second;
		}
	}

	return changed;
}

} // namespace

bool exposeInductionVars(SgForStatement *loop_nest, const InductionVarInfo &info)
{
	if(!loop_nest || info.exposable_divs.empty() || info.derived_order.empty())
		return false;

	LoopNestAttribute *attr = dynamic_cast<LoopNestAttribute*>(loop_nest->getAttribute("LoopNestInfo"));
	if(!attr)
		return false;

	int loop_nest_size = attr->get_nest_size();
	Rose_STL_Container<SgNode*> inner_loops = NodeQuery::querySubTree(loop_nest, V_SgForStatement);
	if (loop_nest_size <= 0 || (int)inner_loops.size() < loop_nest_size)
		return false;
	SgForStatement *innermost_loop = isSgForStatement(inner_loops[loop_nest_size - 1]);
	if (!innermost_loop)
		return false;
	SgBasicBlock *body = isSgBasicBlock(innermost_loop->get_loop_body());
	if(!body)
		return false;

	bool changed = false;
	Rose_STL_Container<SgNode*> blocks = NodeQuery::querySubTree(body, V_SgBasicBlock);
	for(auto block_it = blocks.begin(); block_it != blocks.end(); block_it++)
	{
		SgBasicBlock *block = isSgBasicBlock(*block_it);
		if(!block)
			continue;
		if(exposeBasicBlock(info, block))
			changed = true;
	}

	if(changed)
	{
		SageInterface::fixVariableReferences(loop_nest);
		SageInterface::constantFolding(loop_nest);
		AstPostProcessing(loop_nest);
		log_info("Induction exposure completed");
	}

	return changed;
}

bool runInductionVarPass(SgForStatement *loop_nest)
{
	InductionVarInfo info;
	if(!analyzeInductionVars(loop_nest, info))
		return false;

	return exposeInductionVars(loop_nest, info);
}
