#include "pass/NestCollectPass.hpp"
#include "normalize/normalize.hpp"
#include "preprocess/preprocess.hpp"
#include "DEBUG/debugTool.h"
#include "loop_attr.hpp"
#include <algorithm>
#include <climits>
#include <rose.h>

#include "logger.h"

namespace c2cuda {

namespace {

// 收集单层循环的迭代变量、上界表达式与符号常量，写入 attr
void collectLoopInfo(SgForStatement *loop_nest, LoopNestAttribute *attr)
{
    std::vector<SgInitializedName *> iter_vec, symb_vec;
    std::list<SgExpression *> bound_vec;

    // 获取迭代变量、边界和符号常量
    Rose_STL_Container<SgNode *> inner_loops =
        NodeQuery::querySubTree(loop_nest, V_SgForStatement);
    for (auto inner_it = inner_loops.begin(); inner_it != inner_loops.end(); inner_it++)
    {
        SgForStatement *l = isSgForStatement(*inner_it);
        SgStatement *test_stmt = l->get_test();
        if (!test_stmt || !isSgExprStatement(test_stmt))
            continue;

        // 迭代变量
        iter_vec.push_back(SageInterface::getLoopIndexVariable(l));

        // 上界表达式（归一化后一定是上界）
        SgBinaryOp *test_binop = isSgBinaryOp(l->get_test_expr());
        if (!test_binop)
            continue;
        SgExpression *bound = test_binop->get_rhs_operand();
        bound_vec.push_back(bound);

        // 符号常量：边界表达式中的所有变量引用（去重）
        Rose_STL_Container<SgNode *> v = NodeQuery::querySubTree(bound, V_SgVarRefExp);
        for (auto v_it = v.begin(); v_it != v.end(); v_it++)
        {
            SgInitializedName *var_decl =
                isSgVarRefExp(*v_it)->get_symbol()->get_declaration();
            if (std::find(symb_vec.begin(), symb_vec.end(), var_decl) != symb_vec.end())
                continue;
            symb_vec.push_back(var_decl);
        }
    }

    attr->set_iter_vec(iter_vec);
    attr->set_bound_vec(bound_vec);
    attr->set_symb_vec(symb_vec);
}

} // namespace

bool NestCollectPass::transform(SgProject *project, PassContext &ctx)
{
    /* Fix for(;;) null-test expressions before normalisation touches them */
    fixForLoopTests(project);

    SgFilePtrList &fileList = project->get_fileList();

    /* 对每个文件：收集 + 归一化所有循环嵌套 */
    for (size_t fileIndex = 0; fileIndex < fileList.size(); ++fileIndex)
    {
        SgFile *file = fileList[fileIndex];
        SgSourceFile *sourceFile = isSgSourceFile(file);
        if (!sourceFile)
            continue;
        SgGlobal *fileGlobalScope = sourceFile->get_globalScope();

        // 所有函数定义的 vector 容器，语句必须依附于函数运行，因此从函数体定义入手
        Rose_STL_Container<SgNode *> functions =
            NodeQuery::querySubTree(file, V_SgFunctionDefinition);
        log_info("FUNCTIONS: Get %ld functions, Ready to traverse", functions.size());

        for (auto funcIter = functions.begin(); funcIter != functions.end(); funcIter++)
        {
            // 本函数内收集到的所有最外层循环嵌套
            std::list<SgForStatement *> loopNestList;

            SgFunctionDefinition *defn = isSgFunctionDefinition(*funcIter);
            log_info("--------------------- Enter func %s ---------------------",
                     defn->get_declaration()->get_name().getString().c_str());

            /* 限制每个函数只做一次 imperfect→perfect 转换。
               多次转换会造成 AST 父子关系不一致，
               引发后续 SSA/CFG 分析中 cfgFindChildIndex 断言失败。 */
            int impConvCount = 0;

            Rose_STL_Container<SgNode *> forLoops =
                NodeQuery::querySubTree(defn, V_SgForStatement);
            log_info("FOR LOOP: Get %ld for loops, Ready to Convert Imperfectly Nested Loops into Perfect Ones",
                     forLoops.size());

            // 尝试转换函数定义中所有 for 循环为完美 for 循环
            auto for_iter = forLoops.begin();
            while (for_iter != forLoops.end())
            {
                // 取最外层循环
                SgForStatement *loop_nest = isSgForStatement(*for_iter);
                log_debug(">> Enter for Loop: %s", loop_nest->unparseToString().c_str());

                // 获取嵌套大小，用于跳过内层循环
                Rose_STL_Container<SgNode *> inner_loops =
                    NodeQuery::querySubTree(loop_nest, V_SgForStatement);
                int nest_size = inner_loops.size();

                if (isPerfectlyNested(loop_nest) == false)
                {
                    // 每个函数只转换第一个 imperfect nest
                    if (impConvCount > 0)
                    {
                        log_debug("[IMP]Skipping extra imperfect nest (limit 1 per function)");
                    }
                    else
                    {
                        log_info("[IMP]START converting imperfect loop in func=%s",
                                 defn->get_declaration()->get_name().getString().c_str());
                        log_debug("[IMP]Trying to convert imperfectly nested loop into perfectly nested one");
                        std::vector<SgStatement *> perf_loop_nests =
                            convertImperfToPerf(loop_nest);
                        log_debug("[IMP]Converted Imperfectly Nested Loop into Perfectly Nested Loops");

                        // 转换成功则用一系列完美嵌套循环替换原循环
                        if (perf_loop_nests.size() > 0)
                        {
                            SgBasicBlock *bb_new =
                                SageBuilder::buildBasicBlock_nfi(perf_loop_nests);
                            bb_new->set_parent(loop_nest->get_parent());
                            isSgStatement(loop_nest->get_parent())
                                ->replace_statement(loop_nest, bb_new);
                            log_info("[IMP]DONE conversion in func=%s: %zu perfect nests created",
                                     defn->get_declaration()->get_name().getString().c_str(),
                                     perf_loop_nests.size());
                        }
                        impConvCount++;
                    }
                }

                // 跳到下一个最外层循环
                for_iter += nest_size;
            }

            // 重新查询以获得转换后的循环；querySubTree 按深度优先排序，因此收集到的是所有最外层循环
            forLoops = NodeQuery::querySubTree(defn, V_SgForStatement);
            log_info("Ready to Collect Outer Loops");
            for_iter = forLoops.begin();
            while (for_iter != forLoops.end())
            {
                // 取最外层循环
                SgForStatement *loop_nest = isSgForStatement(*for_iter);

                // 获取嵌套大小
                Rose_STL_Container<SgNode *> inner_loops =
                    NodeQuery::querySubTree(loop_nest, V_SgForStatement);
                int nest_size = inner_loops.size();

                // 设置属性（作用于最外层循环）
                loop_nest->setAttribute("LoopNestInfo",
                                        new LoopNestAttribute(nest_size, true));

                // 收集最外层循环，跳到下一个
                loopNestList.push_back(loop_nest);
                for_iter += nest_size;
            }
            log_info("OUTER LOOPS: Get %ld Outer Loops", loopNestList.size());

            // 逐个处理循环嵌套
            for (SgForStatement *loop_nest : loopNestList)
            {
                SgFunctionDefinition *cur_func =
                    SageInterface::getEnclosingFunctionDefinition(loop_nest);
                std::string cur_func_name =
                    cur_func ? cur_func->get_declaration()->get_name().getString() : "?";
                log_info("[PROCESS] nest_id=%d func=%s file=%s loop=%s",
                         tctx_.nest_id, cur_func_name.c_str(),
                         loop_nest->get_file_info()->get_filenameString().c_str(),
                         loop_nest->unparseToString()
                             .substr(0, loop_nest->unparseToString().find('\n'))
                             .c_str());
                log_debug(">> Processing Loop Nest: %s",
                          loop_nest->unparseToString().c_str());

                // 含 goto 的循环跳过（内联代码产物）
                Rose_STL_Container<SgNode *> gotos =
                    NodeQuery::querySubTree(loop_nest, V_SgGotoStatement);
                if (!gotos.empty())
                {
                    log_info("Loop Nest Skipped (Contains Goto — inlined code)");
                    continue;
                }

                // 转换后仍不完美则跳过
                if (!isPerfectlyNested(loop_nest))
                {
                    log_info("Loop Nest Skipped (Not Perfect)");
                    continue;
                }

                // 获取嵌套属性
                LoopNestAttribute *attr = dynamic_cast<LoopNestAttribute *>(
                    loop_nest->getAttribute("LoopNestInfo"));

                // 归一化
                if (!normalizeLoopNest(loop_nest))
                {
                    log_info("Loop Nest Skipped (Not Normalized)");
                    attr->set_nest_flag(false);
                    continue;
                }

                // 收集迭代变量、上界、符号常量并写入属性
                collectLoopInfo(loop_nest, attr);

                tctx_.qualified.push_back(
                    {loop_nest, attr, cur_func, cur_func_name, fileGlobalScope});
            }

            // 清空本函数的循环列表，进入下一个函数
            loopNestList.clear();
            log_info("--------------------- Exit func %s ---------------------\n",
                     defn->get_declaration()->get_name().getString().c_str());
        }
    }

    return true;
}

} // namespace c2cuda
