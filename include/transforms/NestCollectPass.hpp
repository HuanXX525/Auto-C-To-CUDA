#pragma once

#include "pass/Pass.hpp"
#include "pass/TransformPass.hpp"
#include "pass/TranslateContext.hpp"

namespace c2cuda::transforms {

// 第二遍 Pass1：收集 + 归一化所有循环嵌套，检测合格的嵌套写入 tctx.qualified
class NestCollectPass : public TransformPass {
public:
    explicit NestCollectPass()
        : TransformPass("NestCollect"){}

    bool transform(SgProject *project, PassContext &ctx) override;
};

} // namespace c2cuda
