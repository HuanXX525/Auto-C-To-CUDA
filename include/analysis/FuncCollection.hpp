#pragma once

#include "pass/Pass.hpp"
#include "pass/AnalysisPass.hpp"

namespace c2cuda::analysis {

    /**
     * 收集项目所有的有函数定义的SgFunctionDeclaration
     * 类型：vector<SgFunctionDeclaration*> key：functions
     * 类型：std::map<std::string, int> key：func_order
     */
    class FuncsCollectPass : public c2cuda::AnalysisPass
    {
    public:
        FuncsCollectPass() : c2cuda::AnalysisPass("FuncsCollect") {}

        void analyze(SgProject *project, c2cuda::PassContext &ctx) override;
    };

} // namespace transforms
