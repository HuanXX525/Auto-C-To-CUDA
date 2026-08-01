#pragma once

#include "pass/Pass.hpp"
#include "pass/TransformPass.hpp"
#include "pass/TranslateContext.hpp"
#include "utils/translate_job.h"

namespace c2cuda {

// 第二遍 Pass2：逐文件做 affine/依赖测试与 kernel 代码生成，并注入 CUDA defines
class CodeGenPass : public TransformPass {
public:
    CodeGenPass(TranslateContext &tctx, const TranslateJob &job)
        : TransformPass("CodeGen"), tctx_(tctx), job_(job) {}

    bool transform(SgProject *project, PassContext &ctx) override;

private:
    TranslateContext &tctx_;
    const TranslateJob &job_;
};

} // namespace c2cuda
