#include "transforms/InlinePass.hpp"
#include "preprocess/declarationcopy.h"
#include "preprocess/preprocess.hpp"
#include <inliner.h>
#include <rose.h>

#include "logger.h"

namespace c2cuda::transforms {

bool InlinePass::transform(SgProject *project, PassContext &ctx)
{
    // TODO：收集循环计算量信息
    auto ordered_loop_nests = ctx.getRef<std::vector<SgForStatement *>>("for_loops");
    auto funcOrder = ctx.getRef<std::map<std::string, int>>("func_order");
    for (auto forIter = ordered_loop_nests.begin();
         forIter != ordered_loop_nests.end(); forIter++)
    {
        SgForStatement *forstat = isSgForStatement(*forIter);
        if (!forstat)
            continue;
        log(">> Enter for Loop: \n" + forstat->unparseToString());

        // 递归内联：上一次内联改变了 AST 则继续，直至稳定
        bool changed = true;
        while (changed)
        {
            changed = false;
            Rose_STL_Container<SgNode *> fn_calls =
                NodeQuery::querySubTree(forstat, V_SgFunctionCallExp);

            // 设置函数属性（检查过的不再检查）
            for (auto node = fn_calls.begin(); node != fn_calls.end(); node++)
            {
                SgFunctionCallExp *call = isSgFunctionCallExp(*node);
                if (!call)
                    continue;
                FuncAttribute *f_a = dynamic_cast<FuncAttribute *>(
                    call->getAttribute("FuncAttribute"));
                if (f_a)
                    continue;
                FuncAttribute::getAttributes(call, funcOrder);
            }

            // 内联
            for (auto node = fn_calls.begin(); node != fn_calls.end(); node++)
            {
                SgFunctionCallExp *call = isSgFunctionCallExp(*node);
                if (!call)
                    continue;
                std::string fname =
                    call->getAssociatedFunctionDeclaration()->get_name().getString();
                FuncAttribute *fa = dynamic_cast<FuncAttribute *>(
                    call->getAttribute("FuncAttribute"));
                // 不在 CUDA 白名单、不是纯函数、有定义、不递归、不使用静态变量以及静态函数调用
                if (fa->canInline())
                {
                    // 获取声明阶段获取的是函数定义外所依赖的声明，
                    // 而重命名获取的是函数定义内所依赖的，刚好互不冲突
                    SgFunctionDefinition *def =
                        isSgFunctionDeclaration(call->getAssociatedFunctionSymbol()
                                                    ->get_declaration()
                                                    ->get_definingDeclaration())
                            ->get_definition();
                    std::vector<DeclarationInfo> requiredDeclList =
                        collectDeclarationsForFunction(def);

                    // 内联前做标记，用于寻找内联后的块
                    SgNullStatement *mark = markStatementForInlining(call);

                    bool succ = doInline(call);
                    if (succ)
                    {
                        // 变量重命名
                        renameAfterInline(mark);
                        for (auto decl : requiredDeclList)
                        {
                            addDeclaration(decl, mark);
                        }
                        log("Function Call Inlined Successfully ：" + fname);
                        changed = true;
                    }
                }
            }
        }
    }

    return true;
}

} // namespace c2cuda
