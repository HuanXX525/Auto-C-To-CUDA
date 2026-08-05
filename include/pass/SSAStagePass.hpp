#pragma once

#include "pass/Pass.hpp"
#include "pass/TransformPass.hpp"

namespace c2cuda {

// SSA 阶段 1：诱导变量暴露（project-wide SSA）
class SSAInductionExposePass : public TransformPass {
public:
    explicit SSAInductionExposePass()
        : TransformPass("SSAInductionExpose") {}

    bool transform(SgProject *project, PassContext &ctx) override;
};

// SSA 阶段 2：死代码消除（project-wide SSA）
class SSADeadCodeElimPass : public TransformPass {
public:
    explicit SSADeadCodeElimPass()
        : TransformPass("SSADeadCodeElim") {}

    bool transform(SgProject *project, PassContext &ctx) override;
};

} // namespace c2cuda
