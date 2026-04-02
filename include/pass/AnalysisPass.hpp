#pragma once

#include "pass/Pass.hpp"

namespace c2cuda {

// ===========================================
// AnalysisPass: read-only never change AST
// ===========================================
class AnalysisPass : public Pass {
public:
    using Pass::Pass;

    // the return value means have modify the AST
    bool run(SgProject* project, PassContext& ctx) {
        analyze(project, ctx);
        return false; // read-only Pass
    }

    virtual void analyze(SgProject* project, PassContext& ctx) =0;
};

} // namespace Pass
