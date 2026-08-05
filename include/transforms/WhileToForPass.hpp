#pragma once

#include "pass/Pass.hpp"
#include "pass/TransformPass.hpp"
#include "pass/TranslateContext.hpp"

namespace c2cuda::transforms {

// 第一遍前半：while → for 转换，并产出按函数拓扑序排序的最外层 for 循环列表
class WhileToForPass : public TransformPass {
public:
    explicit WhileToForPass()
        : TransformPass("WhileToFor") {}

    bool transform(SgProject *project, PassContext &ctx) override;

};

} // namespace c2cuda
