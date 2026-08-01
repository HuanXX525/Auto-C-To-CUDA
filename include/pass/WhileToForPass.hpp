#pragma once

#include "pass/Pass.hpp"
#include "pass/TransformPass.hpp"
#include "pass/TranslateContext.hpp"

namespace c2cuda {

// 第一遍前半：while → for 转换，并产出按函数拓扑序排序的最外层 for 循环列表
class WhileToForPass : public TransformPass {
public:
    explicit WhileToForPass(TranslateContext &tctx)
        : TransformPass("WhileToFor"), tctx_(tctx) {}

    bool transform(SgProject *project, PassContext &ctx) override;

private:
    TranslateContext &tctx_;
};

} // namespace c2cuda
