#include "pass/Pass.hpp"
#include "pass/PassManager.hpp"
#include "transforms/SSAStagePass.hpp"
#include "transforms/CodeGenPass.hpp"
#include <rose.h>
#include "transforms/WhileToForPass.hpp"
#include "transforms/InlinePass.hpp"
#include "transforms/NestCollectPass.hpp"
#include "transforms/AnnotateLoop.hpp"
#include "analysis/FuncCollection.hpp"
#include "analysis/LoopCollection.hpp"
#include "analysis/ControlFlowAnalysisPass.hpp"
#include "analysis/PredicationAnalysis.hpp"
#include "transforms/PredicationPassTransformPass.hpp"

namespace c2cuda {

/**
 * 方案1：适用于存在计算密集型任务的项目，通过将计算密集型任务迁移到GPU上来减少CPU负担
 */
PassContext plan1( SgProject* project, const TranslateJob& job, bool isVerbose ) {
    PassManager pm;
    pm.setVerbose(isVerbose);
    bool funcInline = true;

    pm.add<analysis::FuncsCollectPass>();
    pm.add<analysis::ControlFlowAnalysisPass>();
    pm.add<analysis::PredicationAnalysisPass>();
    pm.add<transforms::PredicationTransformPass>();
    pm.add<transforms::WhileToForPass>(); // while→for
    pm.add<analysis::CollectForLoopsPass>(funcInline); // 函数拓扑排序
    if(funcInline){
        pm.add<transforms::InlinePass>();
    }
    pm.add<transforms::NestCollectPass>(); // 循环收集 + 完美嵌套转换 + 归一化
    pm.add<transforms::SSAInductionExposePass>();      // SSA: 诱导变量暴露
    pm.add<transforms::SSADeadCodeElimPass>();         // SSA: 死代码消除
    pm.add<transforms::CodeGenPass>(job);              // affine/依赖测试 + kernel 生成

    pm.run(project);

    // 返回共享上下文，供 main 做摘要输出 / 失败判定
    return pm.getContext();
}

/**
 * 方案2：尝试将整个实例直接迁移到GPU上，一个线程跑一个实例
 */
PassContext plan2(SgProject *project, const TranslateJob &job, bool isVerbose)
{
    // 1. 解决依赖CPU的UDP通信问题，合并两个程序为一个
    // 2. 解决依赖CPU的文件读取问题
    // 3. 分支问题，一个线程映射到一个wrap
}

} // namespace c2cuda
