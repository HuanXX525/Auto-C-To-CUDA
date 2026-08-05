#include "transforms/SSAStagePass.hpp"
#include "pass/TranslateContext.hpp"
#include "preprocess/InductionVariableExposure.h"
#include "preprocess/deadCodeElim.h"
#include "DEBUG/debugTool.h"
#include <rose.h>
#include <staticSingleAssignment.h>

#include "logger.h"

namespace c2cuda::transforms {

bool SSAInductionExposePass::transform(SgProject *project, PassContext &ctx)
{
    const auto &qualified = ctx.getRef<std::vector<QualifiedNest>>("qualified");
    if (qualified.empty())
        return false;

    log_info("[SSA-PHASE1] Building SSA for induction variable exposure (%zu nests)",
             qualified.size());
    fixForLoopTests(project);
    StaticSingleAssignment *ssa = new StaticSingleAssignment(project);
    ssa->run(false, false);
    for (auto &qn : qualified)
        inductionVariableExposure(qn.loop_nest, ssa);
    delete ssa;

    return true;
}

bool SSADeadCodeElimPass::transform(SgProject *project, PassContext &ctx)
{
    const auto &qualified = ctx.getRef<std::vector<QualifiedNest>>("qualified");
    if (qualified.empty())
        return false;

    log_info("[SSA-PHASE2] Rebuilding SSA for dead code elimination");
    fixForLoopTests(project);
    StaticSingleAssignment *ssa = new StaticSingleAssignment(project);
    ssa->run(false, false);
    for (auto &qn : qualified)
        eliminateDeadCode(isSgBasicBlock(qn.loop_nest->get_loop_body()), ssa);
    delete ssa;

    return true;
}

} // namespace c2cuda::transforms
