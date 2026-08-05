#include "pass/Pass.hpp"
#include "pass/PassManager.hpp"
#include <rose.h>
#include "transforms/WhileToForPass.hpp"
#include "transforms/InlinePass.hpp"
#include "transforms/AnnotateLoop.hpp"
#include "analysis/FuncCollection.hpp"
#include "analysis/LoopCollection.hpp"
#include "analysis/ControlFlowAnalysisPass.hpp"
#include "analysis/PredicationAnalysis.hpp"
#include "transforms/PredicationPassTransformPass.hpp"

namespace c2cuda {

void run_pass( SgProject* project, bool isVerbose ) {
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

    bool modified = pm.run(project);

}

} // namespace c2cuda
