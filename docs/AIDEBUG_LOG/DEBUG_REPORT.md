# 调试分析报告: `translate.out` SSA 断言失败

## 问题描述

运行 `./build/bin/translate.out ./test/atax/main.c` 时，程序在 ROSE 的 SSA（Static Single Assignment）分析阶段崩溃：

```
translate.out[16306] 1.26301s Rose[FATAL]: assertion failed:
  ...staticSingleAssignmentCalculation.C:803
  void StaticSingleAssignment::insertDefsForExternalVariables(SgFunctionDeclaration*)
  required: isBuiltinVar(rootName) || isSgClassDefinition(varScope) ||
            isSgNamespaceDefinitionStatement(varScope) || isSgGlobal(varScope)
```

## 调用链分析

通过 GDB 回溯，崩溃的调用链为：

```
#4  StaticSingleAssignment::insertDefsForExternalVariables(SgFunctionDeclaration*)
#5  StaticSingleAssignment::run(bool, bool)
#6  inductionVariableExposure (loop_nest) at src/preprocess/InductionVariableExposure.cpp:484
#7  main at translate.cpp:502
```

## 根因分析

### 触发条件

1. `translate.cpp` 在第二遍处理每个函数中的循环嵌套时，对每个外层循环依次执行：
   - 归纳变量暴露 (inductionVariableExposure, 行502)
   - 死代码消除 (deadCodeElim, 行503)
   - 仿射测试 → 依赖测试 → **内核代码生成 (kernelCodeGenSimple, 行518)**

2. `kernelCodeGenSimple` 调用 `kernelFnDef`（`src/kernel/kernel.cpp:389`），后者创建一个新的 CUDA `__global__` 内核函数，并将其**插入到全局作用域**（`insertStatementBefore`，行551）。

3. 当处理**第二个**循环嵌套时，`inductionVariableExposure` 再次对整个项目运行 SSA（行484）。此时项目的 AST 中已包含第一个循环嵌套生成的 kernel 函数。

4. ROSE SSA 的 `FunctionFilter`（`staticSingleAssignment.h:36`）在决定处理哪些函数时，发现了这个新创建的 kernel 函数声明。由于该函数的 `get_definingDeclaration()` 返回非空指针，且其文件信息标记为非编译器生成，**SSA 决定处理该 kernel 函数**。

5. SSA 在处理该 kernel 函数时，调用 `insertDefsForExternalVariables()`。该函数遍历函数中使用的外部变量，并对每个变量的作用域进行断言检查：
   - 要求作用域必须是 `SgGlobal`、`SgClassDefinition`、`SgNamespaceDefinitionStatement` 或内置变量
   - 但 kernel 函数内部的局部变量（如 `thread_x_id`）和函数参数的作用域是 `SgBasicBlock` 或 `SgFunctionDefinition`，这些类型不在允许列表中
   - **断言失败，程序 abort**

### 问题本质

`kernelFnDef` 在循环处理过程中将新的 kernel 函数插入到 AST。后续循环的 SSA 分析会处理这个新函数，但该函数内部的变量声明不符合 `insertDefsForExternalVariables` 的断言条件，导致崩溃。

## 修复方案

在 `src/kernel/kernel.cpp` 的 `kernelFnDef` 函数中，在将 kernel 函数插入全局作用域之前，将其函数声明的文件信息标记为"编译器生成"：

```cpp
kernel_fn->get_file_info()->setCompilerGenerated();
```

这样，ROSE SSA 的 `FunctionFilter` 在检查到 `isCompilerGenerated() == true` 时会将该函数过滤掉，不再对其进行 SSA 分析。

### 为什么之前的尝试失败

初版修复使用了 `kernel_fn->get_definition()->get_file_info()->setCompilerGenerated()`。这设置了**SgFunctionDefinition 节点**的文件信息，但 SSA 的 `FunctionFilter` 检查的是：

```cpp
SgFunctionDeclaration* definingFunction = 
    isSgFunctionDeclaration(funcDecl->get_definingDeclaration());
bool functionDefinitionIsCompilerGenerated = 
    definingFunction != NULL ? definingFunction->isCompilerGenerated() : false;
```

`definingFunction` 是 `SgFunctionDeclaration`（声明节点），而 `isCompilerGenerated()` 调用的是 `declaration->get_file_info()->isCompilerGenerated()`，**不是** `definition->get_file_info()->isCompilerGenerated()`。这两个文件信息对象是不同的。

因此，必须在**声明节点**（`kernel_fn`）上设置编译生成标记，而不是定义节点。

## 修复位置

- **文件**: `src/kernel/kernel.cpp`
- **行号**: 在 `fixVariableReferences` 之后、`insertStatementBefore` 之前
- **改动**: 添加一行 `kernel_fn->get_file_info()->setCompilerGenerated();`

## 验证

修复后运行 `./build/bin/translate.out ./test/atax/main.c`，程序成功完成所有 6 个外层循环的处理：

- `_auto_kernel_0` (for x 初始化循环): 无依赖 → 生成内核
- `_auto_kernel_1` (for A 初始化循环): 无依赖 → 生成内核
- `_auto_kernel_2` (for y 置零循环): 无依赖 → 生成内核
- `_auto_kernel_3` (for tmp 置零循环): 无依赖 → 生成内核
- tmp 累积循环: 检测到依赖 → 跳过
- y 累积循环: 检测到依赖 → 跳过

输出文件 `test/atax/main.cu` 成功生成，包含 4 个 CUDA kernel 函数定义和相应的 kernel 调用代码。
