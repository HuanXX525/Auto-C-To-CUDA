#pragma once

#include "pass/Pass.hpp"
#include "pass/TransformPass.hpp"

namespace c2cuda::transforms {

    /** 对排序后的循环内函数调用做属性标记与内联
     *  属性名FuncAttribute，标记于相应的函数节点上
     *  */
    // 第一遍后半：
    class InlinePass : public TransformPass
    {
    public:
        explicit InlinePass()
            : TransformPass("Inline") {}

        bool transform(SgProject *project, PassContext &ctx) override;
    };

} // namespace c2cuda
