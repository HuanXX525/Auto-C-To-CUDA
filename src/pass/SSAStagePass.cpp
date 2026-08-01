#include "pass/SSAStagePass.hpp"
#include "preprocess/InductionVariableExposure.h"
#include "preprocess/deadCodeElim.h"
#include "DEBUG/debugTool.h"
#include <rose.h>
#include <staticSingleAssignment.h>

#include "logger.h"

namespace c2cuda {

bool SSAInductionExposePass::transform(SgProject *project, PassContext &ctx)
{
    if (tctx_.qualified.empty())
        return false;

    log_info("[SSA-PHASE1] Building SSA for induction variable exposure (%zu nests)",
             tctx_.qualified.size());
    fixForLoopTests(project);
    StaticSingleAssignment *ssa = new StaticSingleAssignment(project);
    ssa->run(false, false);
    for (auto &qn : tctx_.qualified)
        inductionVariableExposure(qn.loop_nest, ssa);
    delete ssa;

    return true;
}

bool SSADeadCodeElimPass::transform(SgProject *project, PassContext &ctx)
{
    if (tctx_.qualified.empty())
        return false;

    log_info("[SSA-PHASE2] Rebuilding SSA for dead code elimination");
    fixForLoopTests(project);
    StaticSingleAssignment *ssa = new StaticSingleAssignment(project);
    ssa->run(false, false);
    for (auto &qn : tctx_.qualified)
        eliminateDeadCode(isSgBasicBlock(qn.loop_nest->get_loop_body()), ssa);
    delete ssa;

    return true;
}

} // namespace c2cuda
