#pragma once

#include "pass/Pass.hpp"
#include "pass/TransformPass.hpp"

namespace c2cuda::transforms {

class PredicationTransformPass : public TransformPass {
public:
    PredicationTransformPass() : TransformPass("PredicationTransform") {}

    bool transform(SgProject* project, c2cuda::PassContext& ctx) override;
};

} // namespace transforms
