#pragma once

#include "pass/AnalysisPass.hpp"
#include "analysis/ControlFlowInfo.hpp"

namespace c2cuda::analysis {

struct PredicatableIf {
    const IfInfo* info;           // 指向 cfg_info 中的数据
    SgExpression* assignLHS;      // 赋值左值
    SgExpression* thenRHS;        // then 分支右值
    SgExpression* elseRHS;        // else 分支右值（无 else 时为 nullptr）
};

class PredicationAnalysisPass : public AnalysisPass {
public:
    PredicationAnalysisPass()
        : AnalysisPass("PredicationAnalysis") {}

    void analyze(SgProject* /*project*/, PassContext& ctx) override;

private:
    bool canPredicate(const IfInfo* info, PredicatableIf& out);

    SgAssignOp* extractAssignOp(SgStatement* stmt);

};

} // namespace c2cuda
