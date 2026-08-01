#pragma once

#include "pass/Pass.hpp"
#include "pass/TransformPass.hpp"
#include "pass/TranslateContext.hpp"

namespace c2cuda {

// SSA 阶段 1：诱导变量暴露（project-wide SSA）
class SSAInductionExposePass : public TransformPass {
public:
    explicit SSAInductionExposePass(TranslateContext &tctx)
        : TransformPass("SSAInductionExpose"), tctx_(tctx) {}

    bool transform(SgProject *project, PassContext &ctx) override;

private:
    TranslateContext &tctx_;
};

// SSA 阶段 2：死代码消除（project-wide SSA）
class SSADeadCodeElimPass : public TransformPass {
public:
    explicit SSADeadCodeElimPass(TranslateContext &tctx)
        : TransformPass("SSADeadCodeElim"), tctx_(tctx) {}

    bool transform(SgProject *project, PassContext &ctx) override;

private:
    TranslateContext &tctx_;
};

} // namespace c2cuda
