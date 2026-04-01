/* Implementation of preprocessinf functions */
#include "preprocess/preprocess.hpp"
// #include "preprocess.hpp"

/* Attempts to convert a while-loop nest into a for-loop nest */
SgStatement *convertWhileToFor(SgWhileStmt *loop_nest)
{
	/* Create the bb which will represent the new for-loop nest */
	SgBasicBlock *bb_nest = SageBuilder::buildBasicBlock();
	bb_nest->set_parent(loop_nest->get_parent());

	/* Go through each while loop in the loop nest */
	Rose_STL_Container<SgNode *> while_loops = NodeQuery::querySubTree(loop_nest, V_SgWhileStmt);
	for (size_t i = 0; i < while_loops.size(); i++)
	{
		SgWhileStmt *loop = isSgWhileStmt(while_loops[i]);

		/* First, get the index variable */
		SgVarRefExp *index_var;
		SgStatement *cond_stmt = loop->get_condition();

		/* Check if condition is an SgVarRefExp, or a binop */
		SgExprStatement *cond_expr_stmt = isSgExprStatement(cond_stmt);
		if (!cond_expr_stmt)
			return NULL;

		SgExpression *cond_expr = cond_expr_stmt->get_expression();
		if (isSgVarRefExp(cond_expr))
			index_var = isSgVarRefExp(cond_expr);
		else if (isSgBinaryOp(cond_expr))
		{
			if (isSgVarRefExp(isSgBinaryOp(cond_expr)->get_lhs_operand()))
				index_var = isSgVarRefExp(isSgBinaryOp(cond_expr)->get_lhs_operand());
			else
				return NULL;
		}
		else
			return NULL;

		/* Check if previous statement is an initialization of the index variable -- Keep check within scope */
		SgExprStatement *init_stmt = isSgExprStatement(SageInterface::getPreviousStatement(loop, false));
		if (!init_stmt)
			return NULL;

		SgBinaryOp *prev_expr = isSgBinaryOp(init_stmt->get_expression());
		if (!prev_expr)
			return NULL;

		if (isSgVarRefExp(prev_expr->get_lhs_operand()))
		{
			if (isSgVarRefExp(prev_expr->get_lhs_operand())->get_symbol()->get_declaration() != index_var->get_symbol()->get_declaration())
				return NULL;
		}
		else
			return NULL;

		/* If we get here, the previous statement was an initialization of the index variable */

		/* Now, make sure that there are no writes to the index variable or the bounds variable within the loop body */
		SgBasicBlock *body = isSgBasicBlock(loop->get_body());

		/* Create a set that will hold the index var and the bounds vars */
		std::set<SgInitializedName *> idx_bound_vars;
		idx_bound_vars.insert(index_var->get_symbol()->get_declaration());
		if (isSgBinaryOp(cond_expr))
		{
			Rose_STL_Container<SgNode *> v = NodeQuery::querySubTree(isSgBinaryOp(cond_expr)->get_rhs_operand(), V_SgVarRefExp);
			for (auto it = v.begin(); it != v.end(); it++)
				idx_bound_vars.insert(isSgVarRefExp(*it)->get_symbol()->get_declaration());
		}

		/* Go through every statement in the body of the loop (except the last one) and check if there are any writes to the index/bound vars */
		SgStatementPtrList &body_stmts = body->get_statements();
		for (size_t j = 0; j < body_stmts.size() - 1; j++)
		{
			/* Get the reads/writes in this statement */
			std::set<SgInitializedName *> reads, writes;
			SageInterface::collectReadWriteVariables(body_stmts[j], reads, writes);

			/* Check if any of the writes are to the index/bound vars.  If so, return false. */
			for (auto w_it = writes.begin(); w_it != writes.end(); w_it++)
				if (std::find(idx_bound_vars.begin(), idx_bound_vars.end(), *w_it) != idx_bound_vars.end())
					return NULL;
		}

		/* Check if the last statement is an update to the index variable */
		SgExprStatement *stride_stmt = isSgExprStatement(body_stmts.back());
		if (!stride_stmt)
			return NULL;
		std::set<SgInitializedName *> reads, writes;
		SageInterface::collectReadWriteVariables(stride_stmt, reads, writes);
		if (std::find(writes.begin(), writes.end(), index_var->get_symbol()->get_declaration()) == writes.end())
			return NULL;

		/* If we get here, then the loop passes our structure, so we can convert it into a for loop */
		SgBasicBlock *for_body = SageBuilder::buildBasicBlock();
		for (size_t j = 0; j < body_stmts.size() - 1; j++)
			SageInterface::appendStatement(body_stmts[j], for_body);

		/* If the cond_stmt is just a var ref, convert it into a > 0 binop */
		SgStatement *test_stmt = cond_stmt;
		if (isSgVarRefExp(cond_expr))
			test_stmt = SageBuilder::buildExprStatement(
				SageBuilder::buildGreaterThanOp(
					index_var,
					SageBuilder::buildIntVal(0)));

		SgForStatement *for_loop = SageBuilder::buildForStatement(init_stmt, test_stmt, stride_stmt->get_expression(), for_body);
		for_loop->set_parent(bb_nest);

		/* Append only the first while loop to bb_nest, as the rest will be taken care of inside of the first loop */
		if (i == 0)
			SageInterface::appendStatement(for_loop, bb_nest);
		else
			isSgStatement(loop->get_parent())->replace_statement(loop, for_loop);
	}

	/* If we get here, all of the loops have been successfully converted.  So, we return bb_nest */
	return bb_nest;
}

/* Function to convert imperfectly nested loop into a series of perfectly nested loops */
std::vector<SgStatement *> convertImperfToPerf(SgForStatement *imperf_loop_nest)
{
	/* Create a vector which will store the perfectly nested for loops */
	std::vector<SgStatement *> perf_loop_nests;

	/* Obtain the relevant statements (i.e. Statements with reads/writes to arrays) */
	std::vector<SgStatement *> arr_stmts;
	Rose_STL_Container<SgNode *> arr_refs = NodeQuery::querySubTree(imperf_loop_nest, V_SgPntrArrRefExp);
	for (auto arr_it = arr_refs.begin(); arr_it != arr_refs.end(); arr_it++)
	{
		/* Find enclosing statement and append to vector of statements, if not already there */
		SgStatement *arr_stmt = SageInterface::getEnclosingStatement(*arr_it);

		if (std::find(arr_stmts.begin(), arr_stmts.end(), arr_stmt) == arr_stmts.end())
			arr_stmts.push_back(arr_stmt);
	}

	/* Create a pseudo-bb in order to perform some analysis */
	SgBasicBlock *pseudo_bb = SageBuilder::buildBasicBlock_nfi(arr_stmts);
	pseudo_bb->set_parent(imperf_loop_nest->get_parent());

	/* Obtain a graph and the SCCs for the FLOW dependencies */
	Graph *dep_graph_flow = getDependencyGraph(pseudo_bb);
	std::list<std::list<int>> scc_list_flow = dep_graph_flow->getSCCs();

	/* Much like the loop fission case, if each SCC contains only one node, we can transform the loop into a series of perfectly nested ones */
	for (auto scc_it = scc_list_flow.begin(); scc_it != scc_list_flow.end(); scc_it++)
		if ((*scc_it).size() > 1)
			return std::vector<SgStatement *>(); /* Return empty vector to indicate failure */

	/* If we get here, we can perform the transformation for each statement in the SCC list */

	/* Obtain each of the for loops */
	Rose_STL_Container<SgNode *> for_loops = NodeQuery::querySubTree(imperf_loop_nest, V_SgForStatement);

	/* For each of the loops, obtain the index variable so that we can associate the arr_stmts with the proper loops */
	std::vector<SgInitializedName *> index_vars;
	for (auto f_it = for_loops.begin(); f_it != for_loops.end(); f_it++)
		index_vars.push_back(SageInterface::getLoopIndexVariable(*f_it));

	/* If there are any writes to a non-array/non-index variable, return an empty list to be conservative */
	std::set<SgInitializedName *> read_vars, write_vars;
	SageInterface::collectReadWriteVariables(imperf_loop_nest, read_vars, write_vars);
	for (auto wv_it = write_vars.begin(); wv_it != write_vars.end(); wv_it++)
		if (!isSgArrayType((*wv_it)->get_type()))
			if (std::find(index_vars.begin(), index_vars.end(), *wv_it) == index_vars.end())
				return std::vector<SgStatement *>(); /* Return empty vector to indicate failure */

	/* Go through each arr_stmt and create the proper loop_nest */
	for (auto arr_it = arr_stmts.begin(); arr_it != arr_stmts.end(); arr_it++)
	{
		SgStatement *s = *arr_it;

		/* Set to hold the position of the index variable in index_vars, so we it is sorted and we can construct the proper loop nest */
		std::set<int> index_pos;

		/* Find the references to any variables to determine which loop this statement belongs to */
		Rose_STL_Container<SgNode *> var_refs = NodeQuery::querySubTree(s, V_SgVarRefExp);
		for (auto v_it = var_refs.begin(); v_it != var_refs.end(); v_it++)
		{
			auto var_iter = std::find(index_vars.begin(), index_vars.end(), isSgVarRefExp(*v_it)->get_symbol()->get_declaration());
			if (var_iter != index_vars.end())
				index_pos.insert(std::distance(index_vars.begin(), var_iter));
		}

		/* Now, index_pos holds the positions within index_vars which allows us to get the proper loop */

		if (index_pos.size() == 0)
			return std::vector<SgStatement *>(); /* Returning an empty vector to show conversion failed */

		/* Go thru index_pos to construct the loop nests */
		SgForStatement *new_loop_nest;
		for (auto idx_rit = index_pos.rbegin(); idx_rit != index_pos.rend(); idx_rit++)
		{
			/* Copy over new_loop_nest into a temp var */
			SgForStatement *temp = new_loop_nest;

			/* Set new_loop_nest to the loop we are copying */
			new_loop_nest = isSgForStatement(SageInterface::copyStatement(isSgStatement(for_loops[*idx_rit])));

			/* If we are dealing with the inner-most loop, set the body to the statement */
			if (idx_rit == index_pos.rbegin())
				new_loop_nest->set_loop_body(s);
			/* Otherwise, set it equal to temp */
			else
				new_loop_nest->set_loop_body(temp);
		}

		/* Add the newly created loop nest to the vector of perfectly nested loops */
		perf_loop_nests.push_back(new_loop_nest);
	}

	/* If we get here, we have successfully converted the imperfectly-nested loop into a series of perfectly-nested ones, so return that series */
	return perf_loop_nests;
}

/* Function to determine if loop nest is perfectly nested */
bool isPerfectlyNested(SgForStatement *loop_nest)
{
	/* Obtain the loops in the nest */
	Rose_STL_Container<SgNode *> inner_loops = NodeQuery::querySubTree(loop_nest, V_SgForStatement);

	/* Check if loop is imperfectly nested */
	for (size_t idx = 0; idx < inner_loops.size() - 1; idx++)
	{
		SgBasicBlock *body_bb = isSgBasicBlock(isSgForStatement(inner_loops[idx])->get_loop_body());

		if (body_bb)
		{
			/* Get the statements in the body of the loop */
			SgStatementPtrList &loop_stmts = body_bb->get_statements();

			/* If there is more than one statement in the body, the nest is imperfect */
			if (loop_stmts.size() > 1)
				return false;
		}
	}

	/* If we get here, none of the loops (aside from the inner-most one) had more than one statement, so the loop is perfectly nested */
	return true;
}
#include "logger.h"
// bool isRecursive(SgFunctionDeclaration *func)
// {
// 	std::string name = func->get_name();
// 	SgFunctionDefinition *func_def = func->get_definition();
// 	Rose_STL_Container<SgNode *> fn_calls = NodeQuery::querySubTree(func_def, V_SgFunctionCallExp);
// 	for (SgNode *node : fn_calls)
// 	{
// 		SgFunctionCallExp *call = isSgFunctionCallExp(node);
// 		if (!call)
// 			continue;

// 		// 1. 函数名
// 		SgFunctionSymbol *symbol = call->getAssociatedFunctionSymbol();
// 		if (!symbol)
// 		{
// 			// 如果找不到符号（例如复杂的函数指针调用），为了保守起见，返回 false
// 			log_info("getAssociatedFunctionSymbol flase Skip");
// 			break;
// 		}
// 		std::string funcName = symbol->get_name().getString();
// 		if(funcName.compare(name) == 0)
// 			return true;
// 	}
// 	return false;
// }

bool haveDefination(SgFunctionDeclaration *func)
{
	return func->get_definition() != nullptr;
}

#include <iostream>
#include <vector>
#include <map>
#include <queue>
#include <set>
// #include "logger.h"
// #include "preprocess.hpp"
#include "fileio/io.h"
std::map<std::string, int> performTopologicalSort(CallGraphBuilder &CGBuilder)
{
	SgIncidenceDirectedGraph *graph = CGBuilder.getGraph(); // [cite: 145]
	// 获取声明到图节点的映射
	boost::unordered_map<SgFunctionDeclaration *, SgGraphNode *> &nodeMap = CGBuilder.getGraphNodesMapping();

	std::map<SgGraphNode *, int> inDegree;
	std::queue<SgGraphNode *> zeroInDegreeQueue;
	std::vector<SgFunctionDeclaration *> sortedFunctions;
	std::map<std::string, int> sortedFunc;
	std::set<SgGraphNode *> userNodes;

	// 1. 初始化入度：只统计用户节点
	for (auto const &[decl, node] : nodeMap)
	{
		userNodes.insert(node);
	}

	for (SgGraphNode *node : userNodes)
	{
		std::set<SgDirectedGraphEdge *> inEdges = graph->computeEdgeSetIn(node);
		int degree = 0;
		for (auto *edge : inEdges)
		{
			// 只有当调用者也是用户函数，且不是自环时计入入度 [cite: 7]
			if (edge->get_from() != node && userNodes.count(edge->get_from()))
			{
				degree++;
			}
		}
		inDegree[node] = degree;
		if (degree == 0)
			zeroInDegreeQueue.push(node);
	}

	// 2. Kahn 算法逻辑 [cite: 5, 6]
	std::set<SgGraphNode *> processed;
	while (!zeroInDegreeQueue.empty())
	{
		SgGraphNode *curr = zeroInDegreeQueue.front();
		zeroInDegreeQueue.pop();
		processed.insert(curr);

		SgFunctionDeclaration *decl = isSgFunctionDeclaration(curr->get_SgNode());
		if (decl)
			sortedFunctions.push_back(decl);

		std::vector<SgGraphNode *> successors;
		graph->getSuccessors(curr, successors);
		for (SgGraphNode *next : successors)
		{
			if (userNodes.count(next))
			{
				inDegree[next]--;
				if (inDegree[next] == 0)
					zeroInDegreeQueue.push(next);
			}
		}
	}

	// 3. 输出排序结果
	// cout << "\n>>> Optimized Function Processing Order (Top-Down):" << endl;
	// log_info(">>> Optimized Function Processing Order (Top-Down):]n");
	int order = 0;
	for (auto *decl : sortedFunctions)
	{
		// cout << "  [Order] " << decl->get_name().getString() << endl;
		sortedFunc.insert(std::pair<std::string, int>(decl->get_name().getString(), order++));
	}

	// 检查递归（环路）
	for (SgGraphNode *node : userNodes)
	{
		if (processed.find(node) == processed.end())
		{
			SgFunctionDeclaration *d = isSgFunctionDeclaration(node->get_SgNode());
			log_debug("[Warning] Cycle detected involving: %s", d->get_name().getString().c_str());
		}
	}

	return sortedFunc;
}

bool FuncAttribute::_internalStaticFunctionCall(SgFunctionDefinition *funDef)
{
	if (!funDef)
		return false;

	// 1. 查找函数体内所有的函数调用表达式
	VariantVector vv(V_SgFunctionCallExp);
	Rose_STL_Container<SgNode *> callExps = NodeQuery::querySubTree(funDef, vv);

	for (SgNode *node : callExps)
	{
		SgFunctionCallExp *callExp = isSgFunctionCallExp(node);
		if (!callExp)
			continue;

		// 2. 获取被调用函数的声明
		// get_referenced_function_declaration 会处理符号解析，找到对应的函数声明
		SgFunctionSymbol *symbol = callExp->getAssociatedFunctionSymbol();
		SgFunctionDeclaration *targetDecl = symbol->get_declaration();

		if (!targetDecl)
			continue;

		// 3. 检查存储修饰符 (Storage Modifier)
		// 在 C 语言中，static 修饰函数意味着 Internal Linkage
		SgStorageModifier &storageMod = targetDecl->get_declarationModifier().get_storageModifier();

		if (storageMod.isStatic())
		{
			std::string funcName = targetDecl->get_name().str();

			// 获取该函数定义的文件位置（可选，用于验证是否在同一个编译单元）
			// std::string fileName = targetDecl->get_file_info()->get_filename();

			std::cout << "检测到内部链接静态函数调用: " << funcName << std::endl;

			// 一旦发现即返回，或者你可以记录下来做进一步分析
			return true;
		}
	}

	return false;
}

bool FuncAttribute::_static_var(SgFunctionDefinition *funDef)
{
	if (!funDef)
		return false;

	// 1. 获取函数体内所有的变量引用节点
	VariantVector vv(V_SgVarRefExp);
	Rose_STL_Container<SgNode *> varRefs = NodeQuery::querySubTree(funDef, vv);

	for (SgNode *node : varRefs)
	{
		SgVarRefExp *varRef = isSgVarRefExp(node);
		if (!varRef)
			continue;

		// 2. 获取变量对应的初始化声明
		SgVariableSymbol *sym = varRef->get_symbol();
		if (!sym)
			continue;

		SgInitializedName *initName = sym->get_declaration();
		if (!initName)
			continue;

		// 3. 获取声明语句（处理所在的 DeclarationStatement）
		SgVariableDeclaration *varDecl = isSgVariableDeclaration(initName->get_parent());
		if (!varDecl)
			continue;

		// 4. 检测存储修饰符 (Storage Modifier)
		SgStorageModifier &storageMod = varDecl->get_declarationModifier().get_storageModifier();

		if (storageMod.isStatic())
		{
			// 找到了静态变量使用
			std::string varName = initName->get_name().str();

			// 区分是函数内还是全局
			SgScopeStatement *scope = varDecl->get_scope();
			if (isSgFunctionDefinition(scope) || isSgBasicBlock(scope))
			{
				std::cout << "检测到函数内静态变量: " << varName << std::endl;
			}
			else if (isSgGlobal(scope))
			{
				std::cout << "检测到文件级(全局)静态变量: " << varName << std::endl;
			}
			return true;
		}
	}

	return false;
}

bool FuncAttribute::_pure_function(SgFunctionDefinition *funDef){
	return false;
}

void FuncAttribute::getAttributes(SgFunctionCallExp *call, std::map<std::string, int> &funcOrder)
{
	static std::vector<std::string> safe_funcs = Config::getInstance().getSafeFunctions();
	static std::vector<std::string> pure_funcs = Config::getInstance().getPureFunctions();
	// 1. 标注是否是CUDA支持的数学库函数
	SgFunctionSymbol *symbol = call->getAssociatedFunctionSymbol();
	std::string funcName = symbol->get_name().getString();
	FuncAttribute *fa = new FuncAttribute(
		std::find(safe_funcs.begin(), safe_funcs.end(), funcName) != safe_funcs.end());
	// 2. 标注是否能找到定义以及是否递归
	SgFunctionDeclaration *decl = symbol->get_declaration();
	if (!decl)
	{
		log_info("Can't find declaration of function %s", funcName.c_str());
		return;
	}
	SgFunctionDeclaration *definingDecl = isSgFunctionDeclaration(decl->get_definingDeclaration());
	if (definingDecl && definingDecl->get_definition())
	{
		fa->setDefination(true);
		// 未在拓扑排序中列出的表示存在环，不能内联
		bool r = !funcOrder.count(funcName);
		fa->setRecursive(r);
		if(!r){
			// 3. 标注是否存在静态变量的使用
			SgFunctionDefinition *funDef = definingDecl->get_definition();
			fa->setStaticVar(_static_var(funDef));
			// 4. 标注是否存在静态函数的调用
			fa->setStaticFuncCall(_internalStaticFunctionCall(funDef));
			// 5. 标注是否是纯函数
			fa->setPure(std::find(safe_funcs.begin(), safe_funcs.end(), funcName) != safe_funcs.end() || _pure_function(funDef));
		}
		// fa->setRecursive(!funcOrder.count(funcName) || isRecursive(definingDecl));
	}
	else
	{
		fa->setDefination(false);
	}
	call->setAttribute("FuncAttribute", fa);
}
