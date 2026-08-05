#include "transforms/WhileToForPass.hpp"
#include "preprocess/preprocess.hpp"
#include <climits>
#include <rose.h>

#include "logger.h"

namespace c2cuda::transforms {

bool WhileToForPass::transform(SgProject *project, PassContext &ctx)
{
    // Rose_STL_Container<SgNode *> functions =
    //     NodeQuery::querySubTree(project, V_SgFunctionDefinition);
    // log_info("FUNCTIONS: Get %ld functions, Ready to traverse", functions.size());
    auto functions = ctx.getRef<std::vector<SgFunctionDeclaration *>>("functions");
    for (auto funcIter = functions.begin(); funcIter != functions.end(); funcIter++)
    {
        SgFunctionDefinition *defn = isSgFunctionDefinition((*funcIter)->get_definition());
        log("Enter func " + defn->get_declaration()->get_name().getString());

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
        // Rose_STL_Container<SgNode *> forLoops =
        //     NodeQuery::querySubTree(defn, V_SgForStatement);
        // orderedLoopNestList.insert(orderedLoopNestList.end(),
        //                            forLoops.begin(), forLoops.end());
    }



    return true;
}

} // namespace c2cuda
