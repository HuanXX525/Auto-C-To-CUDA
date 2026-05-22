# 调试分析报告

## 概述

在项目 `make debug -j4` 编译后，运行 `./build/bin/translate.out` 处理 PolyBench 测试用例时遇到两个崩溃问题：

1. **`test/atax/main.c`**: SSA 断言失败 (`insertDefsForExternalVariables`)
2. **`test/2mm/main.c`**: CFG 断言失败 (`cfgFindChildIndex`)

---

## 问题 1: atax/main.c — SSA 断言崩溃

### 现象

```
Rose[FATAL]: assertion failed:
  staticSingleAssignmentCalculation.C:803
  void StaticSingleAssignment::insertDefsForExternalVariables(SgFunctionDeclaration*)
  required: isBuiltinVar(rootName) || isSgClassDefinition(varScope) ||
            isSgNamespaceDefinitionStatement(varScope) || isSgGlobal(varScope)
```

GDB 回溯：
```
#4  StaticSingleAssignment::insertDefsForExternalVariables(SgFunctionDeclaration*)
#5  StaticSingleAssignment::run(bool, bool)
#6  inductionVariableExposure at InductionVariableExposure.cpp:484
#7  main at translate.cpp:502
```

### 根因

`translate.cpp` 依次处理每个函数中的外层循环。对每个无依赖关系的循环，调用 `kernelCodeGenSimple`，其内部调用 `kernelFnDef`（`src/kernel/kernel.cpp:389`）创建一个 CUDA `__global__` kernel 函数并**插入到全局作用域**。

处理下一个循环时，`inductionVariableExposure` 再次对整个项目运行 SSA。此时项目 AST 中包含新创建的 kernel 函数。ROSE SSA 的 `FunctionFilter` 处理该 kernel 函数，调用 `insertDefsForExternalVariables` 检查外部变量作用域：

- 断言要求变量作用域为 `SgGlobal`、`SgClassDefinition`、`SgNamespaceDefinitionStatement` 或内置变量
- kernel 函数内部的局部变量（如 `thread_x_id`）和参数的作用域为 `SgBasicBlock` / `SgFunctionDefinition`，不符合要求

### 修复

**文件**: `src/kernel/kernel.cpp`（已提交为 commit `266d57d`）

在 kernel 函数插入全局作用域之前，将其声明标记为编译器生成：

```cpp
kernel_fn->get_file_info()->setCompilerGenerated();
```

ROSE SSA 的 `FunctionFilter`（`staticSingleAssignment.h:64`）检查到 `isCompilerGenerated() == true` 时会过滤该函数，跳过 SSA 处理。

**注意**：该标记必须设置在**函数声明节点**（`kernel_fn`）上，而非定义节点（`kernel_fn->get_definition()`）。SSA 过滤器检查的是 `definingFunction->isCompilerGenerated()`，它读取声明节点的 `get_file_info()`，与定义节点的 `get_file_info()` 是不同的对象。

---

## 问题 2: 2mm/main.c — CFG 断言崩溃

### 现象

```
Rose[FATAL]: assertion failed:
  virtualCFG/memberFunctions.C:410
  virtual unsigned int SgStatement::cfgFindChildIndex(SgNode*)
  required: idx != Rose::INVALID_INDEX
```

GDB 回溯可确认崩溃发生在 SSA 的 CFG（控制流图）遍历阶段，即在构建 CFG 遍历 AST 时找不到子节点在父节点中的索引。

### 根因

该崩溃是原始代码的**预存 bug**（非新引入）。

2mm 测试用例中，`main()` 函数包含**两个**非完美嵌套循环（imperfectly nested loops）。`convertImperfToPerf` 将每个非完美嵌套循环转换为一系列完全嵌套循环，并通过 `replace_statement` 替换原循环。

第一次转换后 AST 正常，SSA 可正常处理。**第二次转换**后，第二次 `convertImperfToPerf` → `replace_statement` 修改已包含第一次转换结果的 AST，产生的父子节点关系**不一致**（child 的 `get_parent()` 指针与父节点的 child list 不匹配），导致后续 SSA 的 CFG 遍历断言失败。

诊断过程确认：
- 禁用第二次转换 → 程序正常运行
- 仅执行第二次转换（跳过第一次）→ 同样崩溃
- 单次转换的 atax 测试 → 正常运行

这表明问题出在**任意一次 `convertImperfToPerf` 在已修改过的 AST 上执行时**的节点管理方式。

经过大量调试尝试（包括 `removeStatement`、`copyStatement`、`resetParentPointers`、`fixVariableReferences`、`constantFolding`、`buildBasicBlock` 替换 `buildBasicBlock_nfi`、`insertStatementBefore` + `removeStatement` 替换 `replaceStatement`、`setCompilerGenerated` 等手段），均无法彻底解决该 ROSE AST 内部一致性断言。

### 修复

**文件**: `translate.cpp`

由于无法可靠地实施多次非完美循环转换，采用**保守限制策略**：每个函数最多转换 1 个非完美嵌套循环。后续非完美循环保持原样，在后续处理中被跳过（"Not Perfect"检查）。

```cpp
int impConvCount = 0;  // 每个函数计数

if (isPerfectlyNested(loop_nest) == false) {
    if (impConvCount > 0) {
        log_debug("[IMP]Skipping extra imperfect nest (limit 1 per function)");
    } else {
        // 执行转换...
        impConvCount++;
    }
}
```

此修复不影响 atax（仅 1 个非完美循环）。对 2mm，第一个非完美循环（tmp 矩阵计算）仍被转换，第二个（D 矩阵计算）被跳过但不影响正确性——仅意味着该循环不被 GPU 并行化。

---

## 修改文件清单

| 文件 | 修改内容 | 状态 |
|------|---------|------|
| `src/kernel/kernel.cpp` | 添加 `kernel_fn->get_file_info()->setCompilerGenerated()` | 已提交 |
| `translate.cpp` | 每函数限制 1 次非完美循环转换 | 未提交 |

---

## 验证

```bash
make debug -j4
./build/bin/translate.out ./test/atax/main.c   # 成功，生成 main.cu (4942 bytes)
./build/bin/translate.out ./test/2mm/main.c    # 成功，生成 main.cu (8566 bytes, 6 kernels)
```
