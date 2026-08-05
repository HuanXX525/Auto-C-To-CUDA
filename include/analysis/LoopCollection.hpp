
#pragma once

#include "pass/Pass.hpp"
#include "pass/AnalysisPass.hpp"

namespace c2cuda::analysis {

    /**
     * 收集最外层For循环并且按照函数调用图拓扑排序，调用栈深的在前
     * Args: sortFor 是否对循环排序
     * key:for_loops
     * std::vector<SgForStatement *>
     */
    class CollectForLoopsPass : public c2cuda::AnalysisPass
    {
    public:
        explicit CollectForLoopsPass(bool sortFor) : c2cuda::AnalysisPass("CollectForLoops"), _sort(sortFor) {}

        void analyze(SgProject *project, c2cuda::PassContext &ctx) override;
    private:
    bool _sort;
};

} // namespace transforms

