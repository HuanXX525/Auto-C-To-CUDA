/*
   Automatic Transcompiler of Affine C Programs to CUDA
   Leart Krasniqi
   August 2020
   Master's Thesis

   This project accepts an affine C program as input and generates CUDA code.
   The steps involved are:
	   1) Loop Nest Conversion
	   a) Loop Nest Conversion
	   b) Normalization
	   c) Affinity Testing
	   2) Dependency Testing
	   a) ZIV Test
	   b) GCD Test
	   c) Banerjee's Test
	   3) Code Generation
	   a) Simple Parallelization
	   b) Loop Fission
	   c) Extended Cycle Shrinking

   All of the relevant methods with detailed explanations are in the ./include directory

   Usage:  ./translate.out [input.c] -rose:o [output.cu]
*/
/**
 * Forked from Automatic Transcompiler of Affine C Programs to CUDA By Leart Krasniqi
 * Tend to add feature multifiles and function analysis.
 *
 * 主流程编排：参数解析 → ROSE frontend → 基础分析 pass → 主流水线 pass →
 * unparse 输出。主流水线各阶段均为独立 Pass（见 include/pass/），
 * 共享状态挂在 TranslateContext 上，便于单独调试与扩展。
 */

#include "include/utils/translate_job.h"
#include "pass/PassManager.hpp"
#include "pass/TranslateContext.hpp"
#include "rose.h"
#include <chrono>
#include <iostream>
#include "logger.h"

int main(int argc, char **argv)
{
	// SgProject::set_verbose(2); // 启用ROSE调试输出
	auto start = std::chrono::high_resolution_clock::now();

    auto build_result = c2cuda::TranslateJobBuilder::build(argc, argv);
    if (build_result.should_exit)
    {
        return build_result.exit_code;
    }

    auto all_args = build_result.args;

    std::vector<std::string> rose_args_storage;
    if (build_result.job.mode == c2cuda::TranslateMode::SingleFile)
    {
        rose_args_storage = build_result.args.remaining;
    }
    else
    {
        const std::string program_name = argc > 0 ? argv[0] : "translate.out";
        rose_args_storage = build_result.job.buildRoseArgv(program_name);
    }

    auto rose_argv = c2cuda::CmdLineParser::toArgv(rose_args_storage);

    if(all_args.hasFlag("verbose")) {
        std::cerr << "The translate command is : ";
        for(auto a : rose_argv) {
            std::cerr << a << " ";
        }
        std::cerr << "\n";
    }

    int rose_argc = static_cast<int>(rose_argv.size());

	ROSE_INITIALIZE;
	SgProject *project = frontend(rose_argc, rose_argv.data());

    // 基础分析 pass + 主流水线（WhileToFor → Inline → NestCollect → SSA×2 → CodeGen）
	log_info("passes manager test");
    c2cuda::PassContext ctx = c2cuda::run_pass(
        project, build_result.job, all_args.hasFlag("verbose"));

	SgFilePtrList &fileList = project->get_fileList();
	if (fileList.empty())
	{
		return 0;
	}

	if (ctx.getOr<bool>("failed", false))
	{
		return 1;
	}

	/* 输出翻译结果 */
	project->unparse();

	/* 统计耗时 */
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    log_info("This transform consumed %lld ms", (long long)ms);

	/* 打印并行化摘要 */
    auto parallelized =
        ctx.getOr<std::vector<c2cuda::ParallelizedLoop>>("parallelized_loops", {});
	log_info("==================== Parallelization Summary ====================");
	log_info("Total parallelized loop nests: %zu", parallelized.size());
	for (size_t i = 0; i < parallelized.size(); i++)
	{
        auto &pl = parallelized[i];
		log_info("  [%zu] Function: %s | Nest depth: %d | %s:%d",
				 i + 1, pl.func_name.c_str(), pl.nest_size,
				 pl.file_name.c_str(), pl.line);
	}
	log_info("=================================================================");
    // fflush(stdout);
	return 0;
}
