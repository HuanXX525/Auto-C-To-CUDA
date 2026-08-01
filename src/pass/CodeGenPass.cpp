#include "pass/CodeGenPass.hpp"
#include "affine/affine.hpp"
#include "dependency/dependency.hpp"
#include "parallel/parallel.hpp"
#include "kernel/kernel.hpp"
#include "fileio/io.h"
#include <rose.h>

#include "logger.h"

namespace c2cuda {

namespace {

// 向文件全局作用域注入 CUDA 相关 #define
void addCudaDefines(SgGlobal *globalScope)
{
    if (globalScope == nullptr)
        return;

    SgLocatedNode *top_scope = globalScope;
    SgStatement *first_stmt = SageInterface::getFirstStatement(globalScope);
    if (first_stmt)
        top_scope = first_stmt;

    SageBuilder::buildCpreprocessorDefineDeclaration(top_scope,
                                                     "#define CUDA_BLOCK_X 128");
    SageBuilder::buildCpreprocessorDefineDeclaration(top_scope,
                                                     "#define CUDA_BLOCK_Y 1");
    SageBuilder::buildCpreprocessorDefineDeclaration(top_scope,
                                                     "#define CUDA_BLOCK_Z 1");
    SageBuilder::buildCpreprocessorDefineDeclaration(top_scope,
                                                     "#define AUTOC2CUDATEST");
}

} // namespace

bool CodeGenPass::transform(SgProject *project, PassContext &ctx)
{
    SgFilePtrList &fileList = project->get_fileList();

    for (size_t fileIndex = 0; fileIndex < fileList.size(); ++fileIndex)
    {
        SgFile *file = fileList[fileIndex];
        SgSourceFile *sourceFile = isSgSourceFile(file);
        if (!sourceFile)
            continue;
        SgGlobal *fileGlobalScope = sourceFile->get_globalScope();

        // 计算输出路径并重命名为 .cu
        const std::string requestedOutput = job_.getOutputPath(
            sourceFile->get_sourceFileNameWithPath());
        if (!renameToCU(sourceFile, requestedOutput))
        {
            tctx_.failed = true;
            return true;
        }

        bool ecs_fn_flag = false;

        // 对该文件下的每个合格嵌套：affine 测试 → 依赖测试 → 代码生成
        for (auto &qn : tctx_.qualified)
        {
            if (qn.file_global_scope != fileGlobalScope)
                continue;

            log_info("[CODGEN] nest_id=%d func=%s",
                     tctx_.nest_id, qn.cur_func_name.c_str());

            if (!affineTest(qn.loop_nest))
            {
                log_info("Loop Nest Skipped (Not Affine)");
                qn.attr->set_nest_flag(false);
                continue;
            }

            switch (dependencyExists(qn.loop_nest))
            {
            case 0: /* 无依赖：直接生成简单 kernel */
                log_info("No Dependency Exists");
                log_info("[KERNEL-GEN] Generating kernel for nest_id=%d func=%s",
                         tctx_.nest_id, qn.cur_func_name.c_str());
                kernelCodeGenSimple(qn.loop_nest, fileGlobalScope, tctx_.nest_id);
                log_info("[KERNEL-GEN] Done kernel for nest_id=%d", tctx_.nest_id);
                tctx_.parallelized_loops.push_back({
                    qn.cur_func->get_declaration()->get_name().getString(),
                    qn.attr->get_nest_size(),
                    qn.loop_nest->unparseToString().substr(
                        0, qn.loop_nest->unparseToString().find('\n'))
                });
                break;

            case 1: /* 存在依赖：尝试提取并行性 */
                log_info("Dependency Exists");
                if (!extractParallelism(qn.loop_nest, fileGlobalScope,
                                        tctx_.nest_id, ecs_fn_flag))
                    log_info("Loop Nest Skipped (Could Not Extract Parallelism");
                else
                    tctx_.parallelized_loops.push_back({
                        qn.cur_func->get_declaration()->get_name().getString(),
                        qn.attr->get_nest_size(),
                        qn.loop_nest->unparseToString().substr(
                            0, qn.loop_nest->unparseToString().find('\n'))
                    });
                break;

            case 2: /* 分析不了 */
                log_info("Loop Nest Skipped (Could Not Determine Dependence)");
                qn.attr->set_nest_flag(false);
                continue;

            default:
                continue;
            }
        }

        // 注入 CUDA #define
        addCudaDefines(fileGlobalScope);
    }

    return true;
}

} // namespace c2cuda
