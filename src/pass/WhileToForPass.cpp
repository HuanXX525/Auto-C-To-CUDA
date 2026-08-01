#include "pass/WhileToForPass.hpp"
#include "preprocess/preprocess.hpp"
#include <CallGraph.h>
#include <climits>
#include <rose.h>

#include "logger.h"

namespace c2cuda {

bool WhileToForPass::transform(SgProject *project, PassContext &ctx)
{
    // 获取函数的拓扑排序，用于对循环进行排序
    std::map<std::string, int> funcOrder;
    {
        CallGraphBuilder CGBuilder(project);
        CGBuilder.buildCallGraph(StrictUserOnlyPredicate());
        funcOrder = performTopologicalSort(CGBuilder);
    }
    tctx_.funcOrder = funcOrder;

    std::vector<SgNode *> orderedLoopNestList;
    Rose_STL_Container<SgNode *> functions =
        NodeQuery::querySubTree(project, V_SgFunctionDefinition);
    log_info("FUNCTIONS: Get %ld functions, Ready to traverse", functions.size());

    for (auto funcIter = functions.begin(); funcIter != functions.end(); funcIter++)
    {
        SgFunctionDefinition *defn = isSgFunctionDefinition(*funcIter);
        log_info("--------------------- Enter func %s ---------------------",
                 defn->get_declaration()->get_name().getString().c_str());

        // 尝试转换函数定义中存在的 while 循环为 for 循环
        Rose_STL_Container<SgNode *> whileLoops =
            NodeQuery::querySubTree(defn, V_SgWhileStmt);
        log_info("WHILE LOOP: Get %ld while loops, Ready to Traverse", whileLoops.size());
        for (auto while_iter = whileLoops.begin();
             while_iter != whileLoops.end();
             /* EMPTY -- Increment at end of loop */)
        {
            // 取最外层的 while 循环
            SgWhileStmt *loop_nest = isSgWhileStmt(*while_iter);
            log_debug(">> Enter while Loop: %s", loop_nest->unparseToString().c_str());

            // 获取嵌套大小，用于跳过内层循环
            Rose_STL_Container<SgNode *> inner_loops =
                NodeQuery::querySubTree(loop_nest, V_SgWhileStmt);
            int nest_size = inner_loops.size();

            // 执行转换
            SgBasicBlock *for_loop_nest =
                isSgBasicBlock(convertWhileToFor(loop_nest));

            // 转换成功则替换原 while 循环
            if (for_loop_nest)
                isSgStatement(loop_nest->get_parent())
                    ->replace_statement(loop_nest, for_loop_nest);

            // 跳到下一个最外层循环
            while_iter += nest_size;
        }

        // 收集该函数内所有的 for 循环
        Rose_STL_Container<SgNode *> forLoops =
            NodeQuery::querySubTree(defn, V_SgForStatement);
        orderedLoopNestList.insert(orderedLoopNestList.end(),
                                   forLoops.begin(), forLoops.end());
    }

    // 按函数拓扑序排序 for 循环
    std::sort(orderedLoopNestList.begin(), orderedLoopNestList.end(),
              [&funcOrder](SgNode *an, SgNode *bn)
              {
                  SgForStatement *a = isSgForStatement(an);
                  SgForStatement *b = isSgForStatement(bn);

                  SgFunctionDefinition *funcA =
                      SageInterface::getEnclosingFunctionDefinition(a);
                  SgFunctionDefinition *funcB =
                      SageInterface::getEnclosingFunctionDefinition(b);

                  std::string nameA =
                      funcA ? funcA->get_declaration()->get_name().getString() : "";
                  std::string nameB =
                      funcB ? funcB->get_declaration()->get_name().getString() : "";

                  // 从拓扑序 map 中获取权重，找不到则排到最后
                  int orderA = funcOrder.count(nameA) ? funcOrder.at(nameA) : INT32_MAX;
                  int orderB = funcOrder.count(nameB) ? funcOrder.at(nameB) : INT32_MAX;

                  return orderA < orderB;
              });
    log_info("Loop Sorted");

    tctx_.ordered_loop_nests = orderedLoopNestList;

    return true;
}

} // namespace c2cuda
