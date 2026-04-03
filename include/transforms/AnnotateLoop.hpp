#pragma once

#include "pass/Pass.hpp"
#include "pass/TransformPass.hpp"
#include "sage3basic.h"

namespace transforms {

class AnnotateLoopPass : public c2cuda::TransformPass {
public:
    AnnotateLoopPass() : TransformPass("AnnotateLoops") {}

    bool transform(SgProject* project, c2cuda::PassContext& ctx) override;
};

} // namespace transforms
