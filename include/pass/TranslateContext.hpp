#pragma once

#include "rose.h"
#include "loop_attr.hpp"
#include <map>
#include <string>
#include <vector>

namespace c2cuda {

// 一个通过全部检测、等待代码生成的循环嵌套
struct QualifiedNest {
    SgForStatement *loop_nest;
    LoopNestAttribute *attr;
    SgFunctionDefinition *cur_func;
    std::string cur_func_name;
    SgGlobal *file_global_scope;
};

// 成功并行化的循环（用于最终摘要输出）
struct ParallelizedLoop {
    std::string func_name;
    int nest_size;
    std::string loop_info;
    std::string file_name;   // 源文件路径
    int line = 0;            // 循环起始行号（原始源码）
};

// 主流水线（WhileToFor → Inline → NestCollect → SSA×2 → CodeGen）
// 各 pass 共享的状态均挂在 TranslateContext 上。
// 由 main 创建并持有，经构造函数引用注入各 pass。
struct TranslateContext {
    // 函数调用拓扑序（WhileToForPass 计算，InlinePass 消费）
    std::map<std::string, int> funcOrder;
    // 按拓扑序排列的最外层 for 循环（WhileToForPass 产出，InlinePass 消费）
    std::vector<SgNode *> ordered_loop_nests;
    // 通过归一化检测、等待代码生成的循环嵌套
    std::vector<QualifiedNest> qualified;
    // 成功并行化摘要
    std::vector<ParallelizedLoop> parallelized_loops;
    // 生成的 kernel 编号（全局递增，防止多文件重复）
    int nest_id = 0;
    // pass 内发生致命错误时置位，main 据此返回非零
    bool failed = false;
};

} // namespace c2cuda
