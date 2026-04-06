#pragma once

#include <rose.h>
#include <vector>
#include <unordered_map>
#include <string>

namespace c2cuda::analysis {

// ============================================================
// 单个 if 语句的结构信息
// ============================================================
struct IfInfo {
    SgIfStmt* node = nullptr;
    SgExpression* condition = nullptr;
    SgStatement* thenBody = nullptr;
    SgStatement* elseBody = nullptr;   // nullptr 表示没有 else

    int thenStmtCount = 0;             // then 分支的语句数
    int elseStmtCount = 0;             // else 分支的语句数
    int nestingDepth  = 0;             // if 嵌套深度
    bool hasFunctionCall = false;      // 分支内是否有函数调用
    bool hasControlFlow  = false;      // 分支内是否有嵌套控制流
    bool hasSideEffect   = false;      // 是否有指针写/volatile 等副作用

    SgForStatement* enclosingLoop = nullptr;  // 所属的最近外层循环
    SgFunctionDefinition* enclosingFunc = nullptr; // 所属函数
};

// ============================================================
// 单个循环的结构信息
// ============================================================
struct LoopInfo {
    SgForStatement* node = nullptr;
    SgExpression* initExpr = nullptr;
    SgExpression* condExpr = nullptr;
    SgExpression* incrExpr = nullptr;
    SgStatement* body = nullptr;

    int bodyStmtCount = 0;
    int ifCount       = 0;             // 循环体内 if 的个数
    int nestingDepth  = 0;             // 循环嵌套深度
    bool isCanonical  = false;         // 是否是规范 for 循环（i=0; i<N; i++）
    bool hasBreak     = false;
    bool hasContinue  = false;
    bool hasGoto      = false;

    SgFunctionDefinition* enclosingFunc = nullptr;
};

// ============================================================
// 函数级的控制流摘要
// ============================================================
struct FunctionCFGSummary {
    SgFunctionDefinition* funcDef = nullptr;
    std::string funcName;

    std::vector<IfInfo> ifs;
    std::vector<LoopInfo> loops;

    int totalIfCount   = 0;
    int totalLoopCount = 0;
    int maxIfDepth     = 0;
    int maxLoopDepth   = 0;
};

// ============================================================
// 整个项目的控制流信息
// ============================================================
struct ControlFlowInfo {
    std::vector<FunctionCFGSummary> functions;

    // 快速索引：SgNode* → 所属的 FunctionCFGSummary
    std::unordered_map<SgFunctionDefinition*, size_t> funcIndex;

    // 便捷查询
    const FunctionCFGSummary* getFunction(SgFunctionDefinition* f) const {
        auto it = funcIndex.find(f);
        if (it == funcIndex.end()) return nullptr;
        return &functions[it->second];
    }

    // 获取所有 IfInfo（跨函数）
    std::vector<const IfInfo*> allIfs() const {
        std::vector<const IfInfo*> result;
        for (auto& fn : functions)
            for (auto& info : fn.ifs)
                result.push_back(&info);
        return result;
    }

    // 获取所有 LoopInfo（跨函数）
    std::vector<const LoopInfo*> allLoops() const {
        std::vector<const LoopInfo*> result;
        for (auto& fn : functions)
            for (auto& info : fn.loops)
                result.push_back(&info);
        return result;
    }
};

} // namespace c2cuda
