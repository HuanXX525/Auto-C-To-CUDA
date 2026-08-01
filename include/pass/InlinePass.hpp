#pragma once

#include "pass/Pass.hpp"
#include "pass/TransformPass.hpp"
#include "pass/TranslateContext.hpp"

namespace c2cuda {

// 第一遍后半：对排序后的循环内函数调用做属性标记与内联
class InlinePass : public TransformPass {
public:
    explicit InlinePass(TranslateContext &tctx)
        : TransformPass("Inline"), tctx_(tctx) {}

    bool transform(SgProject *project, PassContext &ctx) override;

private:
    TranslateContext &tctx_;
};

} // namespace c2cuda
