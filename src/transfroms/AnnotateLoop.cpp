#include "pass/TransformPass.hpp"
#include "rose_attributes_list.h"
#include "sageInterface.h"
#include "transforms/AnnotateLoop.hpp"
#include <rose.h>
#include <vector>

namespace c2cuda::transforms {

bool AnnotateLoopPass::transform(SgProject* projet, c2cuda::PassContext& ctx) {
    // from previous loop collection pass get loops

    auto loops = ctx.get<std::vector<SgForStatement*>>("for_loops");

    if (loops.empty()) {
        log("No loops to annotate");
        return false;
    }

    for (auto loop : loops) {
        SageInterface::attachComment(loop, "C2CUDA: candidate loop ", PreprocessingInfo::before);
        log("Annotated loop an line " 
                + std::to_string(loop->get_file_info()->get_line()) 
                + " at file " 
                + loop->get_file_info()->get_filenameString());
    } 

    return true;
}

} // namespace transforms
