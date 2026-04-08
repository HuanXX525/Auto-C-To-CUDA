#include "pass/Pass.hpp"
#include "pass/PassManager.hpp"
#include <rose.h>

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

    pm.add<analysis::ControlFlowAnalysisPass>();
    pm.add<analysis::PredicationAnalysisPass>();
    pm.add<transforms::PredicationTransformPass>();
    bool modified = pm.run(project);

}

} // namespace c2cuda
