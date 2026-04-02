#pragma once

#include "pass/Pass.hpp"
#include "pass/AnalysisPass.hpp"

namespace transforms {

class FuncsCollectPass : public c2cuda::AnalysisPass {
public:
    FuncsCollectPass() : c2cuda::AnalysisPass("FuncsCollectPass") {}

    void analyze(SgProject* project, c2cuda::PassContext& ctx) override;
};

} // namespace transforms
