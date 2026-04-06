#pragma once

#include "pass/Pass.hpp"

namespace c2cuda {

class TransformPass : public Pass {
public:
    using Pass::Pass; // 继承 Pass 的所有构造函数
    
    bool run(SgProject* project, PassContext& ctx) {
        return transform(project, ctx);
    }

    virtual bool transform(SgProject* project, PassContext& ctx) = 0;
};

} // namespace Pass
