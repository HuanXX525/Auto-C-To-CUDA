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

#include "include/utils/translate_job.h"
#include "pass/PassManager.hpp"
#include "rose.h"
#include <iostream>
#include "logger.h"
#include "loop_attr.hpp"
#include "normalize/normalize.hpp"
#include "affine/affine.hpp"
#include "dependency/dependency.hpp"
#include "parallel/parallel.hpp"
#include "kernel/kernel.hpp"
#include "transforms/InductionVarExposePass.hpp"
#include "preprocess/InductionVariableExposure.h"
#include "preprocess/preprocess.hpp"
#include "preprocess/declarationcopy.h"
#include <inliner.h>
#include <chrono>
#include "fileio/io.h"
#include <atomic>


static std::atomic<unsigned long long> g_inline_uid{0};

void __printSC(SgNode *node)
{
	if (node == nullptr)
		return;
	std::cout << "DEBUG->Print Source Code" << std::endl;
	std::cout << node->unparseToString() << std::endl;
}

int main(int argc, char **argv)
{

    auto start = std::chrono::high_resolution_clock::now();

    auto build_result = c2cuda::TranslateJobBuilder::build(argc, argv);
    if (build_result.should_exit)
    {
        return build_result.exit_code;
    }

    auto all_args = build_result.args;

    std::vector<std::string> rose_args_storage;
    if (build_result.job.mode == c2cuda::TranslateMode::SingleFile)
    {
        rose_args_storage = build_result.args.remaining;
    }
    else
    {
        const std::string program_name = argc > 0 ? argv[0] : "translate.out";
        rose_args_storage = build_result.job.buildRoseArgv(program_name);
    }

    auto rose_argv = c2cuda::CmdLineParser::toArgv(rose_args_storage);
    
    if(all_args.hasFlag("verbose")) {
        std::cerr << "The translate command is : ";
        for(auto a : rose_argv) {
            std::cerr << a << " ";
        }
        std::cerr << "\n";
    }
    

    int rose_argc = static_cast<int>(rose_argv.size());


	ROSE_INITIALIZE;
	SgProject *project = frontend(rose_argc, rose_argv.data());

    // run c2cuda pass
	log_info("passes manager test");
    c2cuda::run_pass(project, true);

	SgFilePtrList &fileList = project->get_fileList();
	if (fileList.empty())
	{
		return 0;
	}

	/* 第一遍while转for、标注函数属性、排序内联 */
	log_info("第一遍");
	{
		// 获取函数的拓扑排序，目前分两遍处理，拓扑排序没什么用，先保留以便后续扩展
		std::map<std::string, int> funcOrder;
		{
			CallGraphBuilder CGBuilder(project);
			CGBuilder.buildCallGraph(StrictUserOnlyPredicate());
			funcOrder = performTopologicalSort(CGBuilder);
		}
		std::vector<SgNode *> orderedLoopNestList;
		Rose_STL_Container<SgNode *> functions = NodeQuery::querySubTree(project, V_SgFunctionDefinition);
		log_info("FUNCTIONS: Get %ld functions, Ready to traverse", functions.size());
		Rose_STL_Container<SgNode *>::const_iterator funcIter = functions.begin();
		// 将块内的while循环转为for循环
		while (funcIter != functions.end())
		{
			/* Get the actual definition node */
			SgFunctionDefinition *defn = isSgFunctionDefinition(*funcIter);
			log_info("--------------------- Enter func %s ---------------------",
					 defn->get_declaration()->get_name().getString().c_str());

			/* Query for any while loops and t r y to convert them int o  fo r loops */
			/* 尝试转化函数定义中存在的while循环为for循环 */
			Rose_STL_Container<SgNode *> whileLoops = NodeQuery::querySubTree(defn, V_SgWhileStmt);
			log_info("WHILE LOOP: Get %ld while loops, Ready to Traverse", whileLoops.size());
			for (auto while_iter = whileLoops.begin(); while_iter != whileLoops.end(); /* EMPTY -- Increment at end of loop */)
			{
				/* Get the outer most while loop */
				SgWhileStmt *loop_nest = isSgWhileStmt(*while_iter);
				log_debug(">> Enter while Loop: %s", loop_nest->unparseToString().c_str());

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
			Rose_STL_Container<SgNode *> forLoops = NodeQuery::querySubTree(defn, V_SgForStatement);
			// 收集所有的for循环
			orderedLoopNestList.insert(orderedLoopNestList.end(), forLoops.begin(), forLoops.end());
			funcIter++;
		}
		// 排序for循环,返回 true 表示 a 应该排在 b 前面
		std::sort(orderedLoopNestList.begin(), orderedLoopNestList.end(),
				  [&funcOrder](SgNode *an, SgNode *bn)
				  {
					  SgForStatement *a = isSgForStatement(an);
					  SgForStatement *b = isSgForStatement(bn);

					  SgFunctionDefinition *funcA = SageInterface::getEnclosingFunctionDefinition(a);
					  // 2. 找到 b 所在的函数定义
					  SgFunctionDefinition *funcB = SageInterface::getEnclosingFunctionDefinition(b);

					  // 3. 获取函数名（注意处理空指针，防止某些 for 不在函数内的极端情况）
					  std::string nameA = funcA ? funcA->get_declaration()->get_name().getString() : "";
					  std::string nameB = funcB ? funcB->get_declaration()->get_name().getString() : "";

					  // 4. 从 map 中获取权重，如果找不到（at 会抛异常，可以用 find）
					  int orderA = funcOrder.count(nameA) ? funcOrder.at(nameA) : INT32_MAX;
					  int orderB = funcOrder.count(nameB) ? funcOrder.at(nameB) : INT32_MAX;

					  // 5. 排序：小的在前面（升序）
					  return orderA < orderB;
				  });
		log_info("Loop Sorted");

		// TODO：收集循环计算量信息
		
		// 内联和函数标记
		for (auto forIter = orderedLoopNestList.begin(); forIter != orderedLoopNestList.end(); forIter++)
		{
			// 查询所有函数调用
			SgForStatement *forstat = isSgForStatement(*forIter);
			if (!forstat)
				continue;
			log_debug(">> Enter for Loop: %s", forstat->unparseToString().c_str());
			// 查询所有函数调用
			// 递归内联如果上一次未改变则中断循环
			bool changed = true;
			while (changed)
			{
				changed = false;
				Rose_STL_Container<SgNode *> fn_calls = NodeQuery::querySubTree(forstat, V_SgFunctionCallExp);
				/* 设置信息 */
				for (auto node = fn_calls.begin(); node != fn_calls.end(); node++)
				{
					SgFunctionCallExp *call = isSgFunctionCallExp(*node);
					if (!call)
						continue;
					// 有属性说明检查过了
					FuncAttribute *f_a = dynamic_cast<FuncAttribute *>(call->getAttribute("FuncAttribute"));
					if (f_a)
						continue;
					FuncAttribute::getAttributes(call, funcOrder);
				}
				/* 内联 */
				for (auto node = fn_calls.begin(); node != fn_calls.end(); node++)
				{
					SgFunctionCallExp *call = isSgFunctionCallExp(*node);
					if (!call)
					continue;
					std::string fname = call->getAssociatedFunctionDeclaration()->get_name().getString();
					FuncAttribute *fa = dynamic_cast<FuncAttribute *>(call->getAttribute("FuncAttribute"));
					// if(!fa) continue;
					/* 不在CUDA白名单、不是纯函数、有定义、不递归、不使用静态变量以及静态函数调用*/
					if (fa->canInline())
					{
						/* 获取声明阶段获取的是函数定义外所依赖的声明，而重命名获取的是函数定义内所依赖的刚好互不冲突 */
						
						SgFunctionDefinition *def = isSgFunctionDeclaration(call->getAssociatedFunctionSymbol()->get_declaration()->get_definingDeclaration())->get_definition();
						std::vector<DeclarationInfo> requiredDeclList =	collectDeclarationsForFunction(def);
						
						// UNUSED:内联前做标记，用于寻找内联后的块
						SgNullStatement *mark = markStatementForInlining(call);
						// 执行内联
						
						// bool succ = true;
						bool succ = doInline(call);
						if (succ)
						{
							// 变量重命名
							renameAfterInline(mark);
							for (auto decl : requiredDeclList)
							{
								addDeclaration(decl, mark);
							}
							log_info("Function Call %s Inlined Successfully", fname.c_str());
							changed = true;
							// changed = false;
						}
					}
				}
			}
		}
	}

	/* Will hold the id number of nests that will be parallelized (to be used to name kernel function) */
	/* 第二遍转化 */
	log_info("第二遍");
	int nest_id = 0; // 防止多文件ID重复
	for (size_t fileIndex = 0; fileIndex < fileList.size(); ++fileIndex)
	{
		/* Obtain the global scope */
		SgGlobal *fileGlobalScope = nullptr; // 生成代码时需要在本文件的域内生成
		SgFile *file = fileList[fileIndex];
		SgSourceFile *sourceFile = isSgSourceFile(file);
		if (sourceFile)
		{
			fileGlobalScope = sourceFile->get_globalScope();
			
            renameToCU(sourceFile);
        }
		else
		{
			continue;
		}

		/* Get all function definitions */
		// 所有函数定义的vector容器，因为语句必须依附于函数运行，因此从函数体定义入手
		Rose_STL_Container<SgNode *> functions = NodeQuery::querySubTree(file, V_SgFunctionDefinition); // 文件内所有的函函数定义节点
		log_info("FUNCTIONS: Get %ld functions, Ready to traverse", functions.size());
		Rose_STL_Container<SgNode *>::const_iterator funcIter = functions.begin();

		/* Will hold each of the loop nests */
		std::list<SgForStatement *> loopNestList;

		/* Flag to see if ecsMinFn and ecsMaxFn have been created already (to be used in parallelism extraction) */
		bool ecs_fn_flag = false;

		/* Loop through each function definition */
		/* 对从单个文件中查询到的所有函数定义Node执行以下操作 */
		while (funcIter != functions.end() && fileGlobalScope != nullptr)
		{
			/* Get the actual definition node */
			SgFunctionDefinition *defn = isSgFunctionDefinition(*funcIter);
			log_info("--------------------- Enter func %s ---------------------",
					 defn->get_declaration()->get_name().getString().c_str());

			Rose_STL_Container<SgNode *> forLoops = NodeQuery::querySubTree(defn, V_SgForStatement);
			/* Check if we can convert any imperf nests into perf ones */
			/* 尝试转化函数定义中所有for循环为完美for循环 */
			log_info("FOR LOOP: Get %ld for loops, Ready to Convert Imperfectly Nested Loops into Perfect Ones", forLoops.size());
			auto for_iter = forLoops.begin();
			while (for_iter != forLoops.end())
			{
				/* Get the outer most loop */
				SgForStatement *loop_nest = isSgForStatement(*for_iter);
				log_debug(">> Enter for Loop: %s", loop_nest->unparseToString().c_str());
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

				loopNestList.push_back(loop_nest);

				/* Increment to get to next loop_nest */
				for_iter += nest_size;
			}
			log_info("OUTER LOOPS: Get %ld Outer Loops", loopNestList.size());
			// nest_id += loopNestList.size(); // 防止多文件下id重复
			/* Iterate through the loop nests */
			std::list<SgForStatement *>::iterator nest_iter;
			for (nest_iter = loopNestList.begin(); nest_iter != loopNestList.end(); nest_iter++)
			{
				SgForStatement *loop_nest = *nest_iter;
				log_debug(">> Processing Loop Nest: %s", loop_nest->unparseToString().c_str());
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
				std::vector<SgInitializedName *> iter_vec, symb_vec;
				std::list<SgExpression *> bound_vec;
				Rose_STL_Container<SgNode *> inner_loops = NodeQuery::querySubTree(loop_nest, V_SgForStatement);
				Rose_STL_Container<SgNode *>::iterator inner_it;
				for (inner_it = inner_loops.begin(); inner_it != inner_loops.end(); inner_it++)
				{
					SgForStatement *l = isSgForStatement(*inner_it);

					/* Iteration variables */
					/* 迭代变量获取 */
					iter_vec.push_back(SageInterface::getLoopIndexVariable(l));

					/* Bounds Expressions */
					/* 获得边界，由于标准化了一定是上界 */
					SgExpression *bound = isSgBinaryOp(l->get_test_expr())->get_rhs_operand();
					bound_vec.push_back(bound);

					/* Symbolic Constants -- Query for any variable references in the bounds expression */
					/* 获取其中的符号常量并存储的集合中 */
					Rose_STL_Container<SgNode *> v = NodeQuery::querySubTree(bound, V_SgVarRefExp); // 边界表达式的变量集合
					for (Rose_STL_Container<SgNode *>::iterator v_it = v.begin(); v_it != v.end(); v_it++)
					{
						SgInitializedName *var_decl = isSgVarRefExp(*v_it)->get_symbol()->get_declaration();

						/* Keep only unique vars */
						if (std::find(symb_vec.begin(), symb_vec.end(), var_decl) != symb_vec.end())
							continue;
						else
							symb_vec.push_back(var_decl);
					}
				}

				/* Append iter_vec, bound_vec, and symb_vec to attributes */
				attr->set_iter_vec(iter_vec);
				attr->set_bound_vec(bound_vec);
				attr->set_symb_vec(symb_vec);

				/* Induction-variable exposure runs after normalization and before affine/dependence checks. */
				// runInductionVarPass(loop_nest);
				inductionVariableExposure(loop_nest);

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
					kernelCodeGenSimple(loop_nest, fileGlobalScope, nest_id);
					break;

				case 1: /* Parallelism Extraction */
					log_info("Dependency Exists");
					if (!extractParallelism(loop_nest, fileGlobalScope, nest_id, ecs_fn_flag))
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
			loopNestList.clear();
			log_info("--------------------- Exit func %s ---------------------\n", defn->get_declaration()->get_name().getString().c_str());
			funcIter++;
			// log_debug("DEBUG:\n %s",defn->unparseToString().c_str());
		}

		/* #define the CUDA_BLOCKs */
		/* 在文件的第一个语句前面添加下面的声明 */
		if (fileGlobalScope != nullptr)
		{
			SgLocatedNode *top_scope = fileGlobalScope;
			SgStatement *first_stmt = SageInterface::getFirstStatement(fileGlobalScope);
			if (first_stmt)
				top_scope = first_stmt;

			SageBuilder::buildCpreprocessorDefineDeclaration(top_scope, "#define CUDA_BLOCK_X 128");
			SageBuilder::buildCpreprocessorDefineDeclaration(top_scope, "#define CUDA_BLOCK_Y 1");
			SageBuilder::buildCpreprocessorDefineDeclaration(top_scope, "#define CUDA_BLOCK_Z 1");
			// 添加宏用于测试，区分是否已经转化
			SageBuilder::buildCpreprocessorDefineDeclaration(top_scope, "#define AUTOC2CUDATEST");
		}
	}

	/* Obtain translation */
	project->unparse();


    // summary the consumed time
    auto end = std::chrono::high_resolution_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    log_info("This transform consumed %lld ms", (long long)ms);


	return 0;
}
