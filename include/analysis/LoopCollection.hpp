
#pragma once

#include "pass/Pass.hpp"
#include "pass/AnalysisPass.hpp"

namespace c2cuda::analysis {

class CollectForLoopsPass : public c2cuda::AnalysisPass {
public:
    CollectForLoopsPass() : c2cuda::AnalysisPass("CollectForLoops") {}

    void analyze(SgProject* project, c2cuda::PassContext& ctx) override;
};

} // namespace transforms

