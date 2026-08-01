#pragma once

#include "pass/Pass.hpp"
#include "pass/TransformPass.hpp"
#include "pass/TranslateContext.hpp"

namespace c2cuda {

// 第二遍 Pass1：收集 + 归一化所有循环嵌套，检测合格的嵌套写入 tctx.qualified
class NestCollectPass : public TransformPass {
public:
    explicit NestCollectPass(TranslateContext &tctx)
        : TransformPass("NestCollect"), tctx_(tctx) {}

    bool transform(SgProject *project, PassContext &ctx) override;

private:
    TranslateContext &tctx_;
};

} // namespace c2cuda
