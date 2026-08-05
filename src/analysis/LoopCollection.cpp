#include <analysis/LoopCollection.hpp>
#include "preprocess/preprocess.hpp"
// #include <CallGraph.h>
#include <climits>
#include <rose.h>
namespace c2cuda::analysis
{

    void CollectForLoopsPass::analyze(SgProject *project, c2cuda::PassContext &ctx)
    {
        std::vector<SgForStatement *> loops;
        auto functions = ctx.getRef<std::vector<SgFunctionDeclaration *>>("functions"); // 获取之前收集的有定义的函数
        for (auto funcIter = functions.begin(); funcIter != functions.end(); funcIter++)
        { // 从函数定义中收集For循环
            SgFunctionDefinition *defn = isSgFunctionDefinition((*funcIter)->get_definition());
            Rose_STL_Container<SgNode *> forLoops = NodeQuery::querySubTree(defn, V_SgForStatement);
            for (auto for_iter = forLoops.begin();
                 for_iter != forLoops.end();
                 /* EMPTY -- Increment at end of loop */)
            {
                // 取最外层的 for 循环
                SgForStatement *loop_nest = isSgForStatement(*for_iter);
                loops.push_back(loop_nest);
                log("Found for-loop at line " + std::to_string(loop_nest->get_file_info()->get_line()));
                // 获取嵌套大小，用于跳过内层循环
                Rose_STL_Container<SgNode *> inner_loops = NodeQuery::querySubTree(loop_nest, V_SgForStatement);
                int nest_size = inner_loops.size();

                // 跳到下一个最外层循环
                for_iter += nest_size;
            }
        }
        log("Total for-loops: " + std::to_string(loops.size()));

        if(_sort){
            // 获取函数的拓扑排序，用于对循环进行排序
            std::map<std::string, int> funcOrder;
            {
                CallGraphBuilder CGBuilder(project);
                CGBuilder.buildCallGraph(StrictUserOnlyPredicate());
                funcOrder = performTopologicalSort(CGBuilder);
            }
                // 按函数拓扑序排序 for 循环
            std::sort(loops.begin(), loops.end(),
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
            log("Loop Sorted");
            ctx.set<std::map<std::string, int>>("func_order", std::move(funcOrder));
        }
        ctx.set<std::vector<SgForStatement *>>("for_loops", std::move(loops));
        // Rose_STL_Container<SgNode *> nodes = NodeQuery::querySubTree(project, V_SgForStatement);

        // for (SgNode *n : nodes)
        // {
        //     if (auto *forStmt = isSgForStatement(n))
        //     {
        //         loops.push_back(forStmt);
        //         log("Found for-loop at line " +
        //             std::to_string(forStmt->get_file_info()->get_line()));
        //     }
        // }

        // 存入上下文，供后续 pass 使用
    }

} // namespace transforms
