/*
   Automatic Transcompiler of Affine C Programs to CUDA
   Leart Krasniqi
   August 2020
   Master's Thesis

   This project accepts an affine C program as input and generates CUDA code.
   The steps involved are:
	   1) Preprocessing
		   a) Loop Nest Conversion
	   b) Normalization
	   c) Affinity Testing
	   2) Dependency Testing
		   a) ZIV Test
	   b) GCD Test
	   c) Banerjee's Test
	   3) Code Generation
		   a) Simple Parallelization
	   b) Loop Fission
	   c) Extended Cycle Shrinking

   All of the relevant methods with detailed explanations are in the ./include directory

   Usage:  ./translate.out [input.c] -rose:o [output.cu]
*/
/**
 * Forked from Automatic Transcompiler of Affine C Programs to CUDA By Leart Krasniqi
 * Tend to add feature multifiles and function analysis.
 */
#include "rose.h"
#include <iostream>
#include "./include/fileio/io.h"
#include "./include/logger.h"
#include "./include/loop_attr.hpp"
#include "./include/normalize/normalize.hpp"
#include "./include/affine/affine.hpp"
#include "./include/dependency/dependency.hpp"
#include "./include/parallel/parallel.hpp"
#include "./include/kernel/kernel.hpp"
#include "./include/preprocess/preprocess.hpp"
#include <inliner.h>
#define DEBUG 0

void __printSC(SgNode* node)
{
	if (node == nullptr) return;
	/////
	std::cout << "DEBUG->Print Source Code" << std::endl;
	std::cout << node->unparseToString() << std::endl;
}


int main(int argc, char **argv)
{
	Config::getInstance().load();
	ROSE_INITIALIZE;
	SgProject *project = frontend(argc, argv);
	/* Obtain the global scope */
	/* 在使用Makefile的项目中将此程序作为编译器时，需要过滤输入不是源文件的情况
	** 并非问题关键，需要解决多文件依赖问题
	*/
	SgGlobal *globalScope = nullptr;
	if (project->get_fileList().empty() == false)
	{
		globalScope = SageInterface::getFirstGlobalScope(project);
		SgFile *file = SageInterface::getEnclosingFileNode(globalScope);
		if (file)
		{
			std::string fileName = file->get_sourceFileNameWithoutPath();
			log_info(">>>> Translating file: %s <<<<", fileName.c_str());
		}
	}
	else
	{
		log_info("No source files detected (Skip Translate).");
	}

	/* Get all function definitions */
	// 所有函数定义的vector容器，因为语句必须依附于函数运行，因此从函数体定义入手
	Rose_STL_Container<SgNode *> functions = NodeQuery::querySubTree(project, V_SgFunctionDefinition);
	log_info("Get %ld functions, Read to Traverse", functions.size());
	Rose_STL_Container<SgNode *>::const_iterator func_iter = functions.begin();

	/* Will hold each of the loop nests */
	std::list<SgForStatement *> loop_nest_list;

	/* Will hold the id number of nests that will be parallelized (to be used to name kernel function) */
	int nest_id = 0;

	/* Flag to see if ecsMinFn and ecsMaxFn have been created already (to be used in parallelism extraction) */
	bool ecs_fn_flag = false;
	
	/* Loop through each function definition */
	/* 对从project中查询到的所有函数定义Node执行以下操作 */
	while (func_iter != functions.end() && globalScope != nullptr)
	{
		/* Get the actual definition node */
		SgFunctionDefinition *defn = isSgFunctionDefinition(*func_iter);
		log_info("--------------------- Enter func %s ---------------------", defn->get_declaration()->get_name().getString().c_str());

		/* Query for any while loops and try to convert them into for loops */
		/* 尝试转化函数定义中存在的while循环为for循环 */
		Rose_STL_Container<SgNode *> whileLoops = NodeQuery::querySubTree(defn, V_SgWhileStmt);
		log_info("Get %ld while loops, Ready to Traverse", whileLoops.size());
		for (auto while_iter = whileLoops.begin(); while_iter != whileLoops.end(); /* EMPTY -- Increment at end of loop */)
		{
			/* Get the outer most while loop */
			SgWhileStmt *loop_nest = isSgWhileStmt(*while_iter);
			log_info(">> Enter while Loop: %s", loop_nest->unparseToString().c_str());

			/* Find loop nest size */
			Rose_STL_Container<SgNode *> inner_loops = NodeQuery::querySubTree(loop_nest, V_SgWhileStmt);
			int nest_size = inner_loops.size();

			/* Perform the conversion */
			SgBasicBlock *for_loop_nest = isSgBasicBlock(convertWhileToFor(loop_nest));

			/* If successful, replace the loop nest with the for_loop */
			if (for_loop_nest)
				isSgStatement(loop_nest->get_parent())->replace_statement(loop_nest, for_loop_nest);

			/* Increment to get to next loop nest */
			while_iter += nest_size;
		}

		/* Query for the for loops */
		Rose_STL_Container<SgNode *> forLoops = NodeQuery::querySubTree(defn, V_SgForStatement);
		log_info("Get %ld for loops, Ready to Mark Safe And Inline", forLoops.size());
		/* 标记白名单函数和内联 */
		for (auto f = forLoops.begin(); f != forLoops.end(); ++f)
		{
			// 查询所有函数调用
			SgForStatement *forstat = isSgForStatement(*f);
			if (!forstat)
				continue;
			log_info(">> Enter for Loop: %s", forstat->unparseToString().c_str());
			// 查询所有函数调用
			bool changed = true;
			std::vector<std::string> safe_funcs = Config::getInstance().getSafeFunctions();
			while (changed)
			{
				changed = false;
				Rose_STL_Container<SgNode *> fn_calls = NodeQuery::querySubTree(forstat, V_SgFunctionCallExp);
				/* 设置信息 */
				for (auto node = fn_calls.begin(); node != fn_calls.end();node++)
				{
					SgFunctionCallExp *call = isSgFunctionCallExp(*node);
					if (!call)
						continue;
					FuncAttribute *f_a = dynamic_cast<FuncAttribute *>(call->getAttribute("FuncAttribute"));
					if (f_a)
						continue;
					// 1. 函数名
					SgFunctionSymbol *symbol = call->getAssociatedFunctionSymbol();
					std::string funcName = symbol->get_name().getString();
					FuncAttribute *fa = new FuncAttribute(
						std::find(safe_funcs.begin(), safe_funcs.end(), funcName) != safe_funcs.end());
					SgFunctionDeclaration *decl = symbol->get_declaration();
					if (!decl)
					{
						log_info("Can't find declaration of function %s", funcName.c_str());
						break;
					}
					// 获取定义
					SgFunctionDeclaration *definingDecl = isSgFunctionDeclaration(decl->get_definingDeclaration());
					if (definingDecl && definingDecl->get_definition())
					{
						fa->setDefination(true);
						fa->setRecursive(isRecursive(definingDecl));
					}
					else
					{
						fa->setDefination(false);
						// fa->setRecursive(fa)
					}
					call->setAttribute("FuncAttribute", fa);
				}
				/* 内联 */
				for (auto node = fn_calls.begin(); node != fn_calls.end(); node++)
				{
					SgFunctionCallExp *call = isSgFunctionCallExp(*node);
					if(!call) continue;
					FuncAttribute *fa = dynamic_cast<FuncAttribute *>(call->getAttribute("FuncAttribute"));
					// if(!fa) continue;
					/* 不在白名单有定义且不递归 */
					if(!fa->isSafe()&&fa->haveDefination()&&!fa->isRecursive()){
						bool succ = doInline(call);
						if(succ){
							log_info("inlined");
							changed = true;
						}
					}
				}
			}
		}

		// project->unparse();
		// assert(false);
		forLoops = NodeQuery::querySubTree(defn, V_SgForStatement);
		/* Check if we can convert any imperf nests into perf ones */
		/* 尝试转化函数定义中所有for循环为完美for循环 */
		log_info("Get %ld for loops, Ready to Convert Imperfectly Nested Loops into Perfect Ones", forLoops.size());
		auto for_iter = forLoops.begin();
		while (for_iter != forLoops.end())
		{
			/* Get the outer most loop */
			SgForStatement *loop_nest = isSgForStatement(*for_iter);
			log_info(">> Enter for Loop: %s", loop_nest->unparseToString().c_str());
			/* Obtain the size of this nest (so we can properly update for_iter) */
			Rose_STL_Container<SgNode *> inner_loops = NodeQuery::querySubTree(loop_nest, V_SgForStatement);
			int nest_size = inner_loops.size();

			/* Check if loop is perfectly nested */
			if (isPerfectlyNested(loop_nest) == false)
			{
				/* Try to convert the nest into a perfect one */
				std::vector<SgStatement *> perf_loop_nests = convertImperfToPerf(loop_nest);

				/* If the size is non-zero, the conversion succeeded, so replace the loop_nest with the series of perfectly nested loops */
				if (perf_loop_nests.size() > 0)
				{
					SgBasicBlock *bb_new = SageBuilder::buildBasicBlock_nfi(perf_loop_nests);
					bb_new->set_parent(loop_nest->get_parent());
					isSgStatement(loop_nest->get_parent())->replace_statement(loop_nest, bb_new);
				}
			}

			/* Move onto the next loop nest */
			for_iter += nest_size;
		}

		/* Re-query to obtain any transformed loop nests */
		/* querySubTree使用深度优先搜索排序查询到的节点，因此此循环收集了所有最外层的循环 */
		forLoops = NodeQuery::querySubTree(defn, V_SgForStatement);
		log_info("Ready to Collect Outer Loops");
		for_iter = forLoops.begin();
		while (for_iter != forLoops.end())
		{
			/* Get the outer most loop */
			SgForStatement *loop_nest = isSgForStatement(*for_iter);

			/* Find loop nest size */
			Rose_STL_Container<SgNode *> inner_loops = NodeQuery::querySubTree(loop_nest, V_SgForStatement);
			int nest_size = inner_loops.size();

			/* Set attributes (applied to outermost loop) */
			loop_nest->setAttribute("LoopNestInfo", new LoopNestAttribute(nest_size, true));

			/* Append loop_nest to list of loop nests */
			// __printSC(*for_iter);

			loop_nest_list.push_back(loop_nest);

			/* Increment to get to next loop_nest */
			for_iter += nest_size;
		}
		log_info("Get %ld Outer Loops", loop_nest_list.size());
		/* Iterate through the loop nests */
		std::list<SgForStatement *>::iterator nest_iter;
		for (nest_iter = loop_nest_list.begin(); nest_iter != loop_nest_list.end(); nest_iter++)
		{
			SgForStatement *loop_nest = *nest_iter;
			log_info("Processing Loop Nest: %s", loop_nest->unparseToString().c_str());
			/* Check if loop nest is perfectly nested */
			bool perf = isPerfectlyNested(loop_nest);

			/* If the nest is imperfect here (i.e. even after we tried transforming it), skip this nest */
			if (!perf)
			{
				log_info("Loop Nest Skipped (Not Perfect)");
				continue;
			}

			/* Obtain the attribute of the nest */
			LoopNestAttribute *attr = dynamic_cast<LoopNestAttribute *>(loop_nest->getAttribute("LoopNestInfo"));

			/* Perform normalization */
			if (!normalizeLoopNest(loop_nest))
			{
				log_info("Loop Nest Skipped (Not Normalized)");
				attr->set_nest_flag(false);
				continue;
			}

			/* Obtain the iteration, bound, and symbolic_constant vectors for the loop nest */
			/* 获取迭代变量、边界和符号常量 */
			std::list<std::string> iter_vec, symb_vec;
			std::list<SgExpression *> bound_vec;
			Rose_STL_Container<SgNode *> inner_loops = NodeQuery::querySubTree(loop_nest, V_SgForStatement);
			Rose_STL_Container<SgNode *>::iterator inner_it;
			for (inner_it = inner_loops.begin(); inner_it != inner_loops.end(); inner_it++)
			{
				SgForStatement *l = isSgForStatement(*inner_it);

				/* Iteration variables */
				/* 迭代变量获取 */
				iter_vec.push_back(SageInterface::getLoopIndexVariable(l)->get_name().getString());

				/* Bounds Expressions */
				/* 获得边界，由于标准化了一定是上界 */
				SgExpression *bound = isSgBinaryOp(l->get_test_expr())->get_rhs_operand();
				bound_vec.push_back(bound);

				/* Symbolic Constants -- Query for any variable references in the bounds expression */
				/* 获取其中的符号常量并存储的集合中 */
				Rose_STL_Container<SgNode *> v = NodeQuery::querySubTree(bound, V_SgVarRefExp); // 边界表达式的变量集合
				for (Rose_STL_Container<SgNode *>::iterator v_it = v.begin(); v_it != v.end(); v_it++)
				{
					std::string var_name = isSgVarRefExp(*v_it)->get_symbol()->get_name().getString(); // 变量的字符串

					/* Keep only unique vars */
					if (std::find(symb_vec.begin(), symb_vec.end(), var_name) != symb_vec.end())
						continue;
					else
						symb_vec.push_back(var_name);
				}
			}

			/* Append iter_vec, bound_vec, and symb_vec to attributes */
			attr->set_iter_vec(iter_vec);
			attr->set_bound_vec(bound_vec);
			attr->set_symb_vec(symb_vec);

			/* Affine test */
			if (!affineTest(loop_nest))
			{
				log_info("Loop Nest Skipped (Not Affine)");
				attr->set_nest_flag(false);
				continue;
			}

			/* Dependency Tests */
			switch (dependencyExists(loop_nest))
			{
			case 0: /* Code Generation */
				log_info("No Dependency Exists");
				kernelCodeGenSimple(loop_nest, globalScope, nest_id);
				break;

			case 1: /* Parallelism Extraction */
				log_info("Dependency Exists");
				if (!extractParallelism(loop_nest, globalScope, nest_id, ecs_fn_flag))
					log_info("Loop Nest Skipped (Could Not Extract Parallelism");

				break;

			case 2: /* Skip Loop Nest */
				log_info("Loop Nest Skipped (Could Not Determine Dependence)");
				attr->set_nest_flag(false);
				continue;

			default: /* Should not reach here, skip loop to be safe */
				continue;
			}
		}
		// TEST
		loop_nest_list.clear();
		log_info("--------------------- Exit func %s ---------------------\n", defn->get_declaration()->get_name().getString().c_str());
		//
#if DEBUG
		std::cout << defn->unparseToString() << std::endl;
#endif

		func_iter++;
	}

	/* #define the CUDA_BLOCKs */
	/* 在文件的第一个语句前面添加下面的声明 */
	if (globalScope != nullptr)
	{
		SgLocatedNode *top_scope = globalScope;
		SgStatement *first_stmt = SageInterface::getFirstStatement(globalScope);
		if (first_stmt)
			top_scope = first_stmt;

		SageBuilder::buildCpreprocessorDefineDeclaration(top_scope, "#define CUDA_BLOCK_X 128");
		SageBuilder::buildCpreprocessorDefineDeclaration(top_scope, "#define CUDA_BLOCK_Y 1");
		SageBuilder::buildCpreprocessorDefineDeclaration(top_scope, "#define CUDA_BLOCK_Z 1");
	}

	/* 对所有文件进行后缀转化 */
	for (int i = 0; i < project->numberOfFiles(); ++i)
	{
		SgFile &file = project->get_file(i);
		SgSourceFile *sourceFile = isSgSourceFile(&file);

		if (sourceFile)
		{
			// 1. 获取原始文件名（例如 "main.c"）
			std::string originalName = sourceFile->get_sourceFileNameWithoutPath();

			// 2. 找到最后一个点号的位置，去掉原后缀
			size_t lastDot = originalName.find_last_of(".");
			std::string baseName = (lastDot == std::string::npos) ? originalName : originalName.substr(0, lastDot);

			// 3. 构造新的输出文件名（例如 "main.cu"）
			std::string newName = baseName + ".cu";

			// 4. 【关键步骤】设置输出文件名
			// 设置后，ROSE 将不再使用默认的 rose_ 前缀逻辑
			sourceFile->set_unparse_output_filename(newName);

			// （可选）如果你想直接控制输出目录，可以设置完整路径
			// sourceFile->set_output_filename("/path/to/output/" + newName);
		}
	}
	/* Obtain translation */
	project->unparse();

	return 0;
}
