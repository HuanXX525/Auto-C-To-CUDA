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
#include "transforms/WhileToForPass.hpp"
// #include "pass/InlinePass.hpp"
#include "pass/NestCollectPass.hpp"
#include "pass/SSAStagePass.hpp"
#include "pass/CodeGenPass.hpp"
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

    // 基础分析 pass（控制流、谓词分析等）
	log_info("passes manager test");
    c2cuda::run_pass(project, all_args.hasFlag("verbose"));

	SgFilePtrList &fileList = project->get_fileList();
	if (fileList.empty())
	{
		return 0;
	}

	/* ────────── 主流水线：WhileToFor → Inline → NestCollect → SSA×2 → CodeGen ────────── */
	// c2cuda::TranslateContext tctx;
	// c2cuda::PassManager pm;
	// pm.setVerbose(all_args.hasFlag("verbose"));
	// pm.add<c2cuda::WhileToForPass>(tctx);       // while→for + 函数拓扑排序
	// pm.add<c2cuda::InlinePass>(tctx);           // 函数属性标记 + 内联
	// pm.add<c2cuda::NestCollectPass>(tctx);      // 循环收集 + 完美嵌套转换 + 归一化
	// pm.add<c2cuda::SSAInductionExposePass>(tctx); // SSA: 诱导变量暴露
	// pm.add<c2cuda::SSADeadCodeElimPass>(tctx);  // SSA: 死代码消除
	// pm.add<c2cuda::CodeGenPass>(tctx, build_result.job); // affine/依赖测试 + kernel 生成
	// pm.run(project);

	// if (tctx.failed)
	// {
	// 	return 1;
	// }

	/* 输出翻译结果 */
	project->unparse();

	/* 统计耗时 */
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    log_info("This transform consumed %lld ms", (long long)ms);

	// /* 打印并行化摘要 */
	// log_info("==================== Parallelization Summary ====================");
	// log_info("Total parallelized loop nests: %zu", tctx.parallelized_loops.size());
	// for (size_t i = 0; i < tctx.parallelized_loops.size(); i++)
	// {
	// 	auto &pl = tctx.parallelized_loops[i];
	// 	log_info("  [%zu] Function: %s | Nest depth: %d",
	// 			 i + 1, pl.func_name.c_str(), pl.nest_size);
	// }
	// log_info("=================================================================");

	return 0;
}
