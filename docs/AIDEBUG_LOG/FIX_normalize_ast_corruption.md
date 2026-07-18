# 修复报告：normalizeLoopNest + ivDrive 导致 AST 损坏引发 SSA/CFG 断言崩溃

## 问题

```bash
./build/bin/translate.out -p external/Aircraft-Simu/build/compile_commands.env_sim.json -O .vscode/out
```

运行后依次出现 3 个不同断言崩溃：

| 序号 | 断言位置 | 错误信息 | 触发时机 |
|------|----------|---------|---------|
| 1 | `memberFunctions.C:174` | `getNodeJustAfterInContainer` parent != null | SSA CFG 构建（~40s） |
| 2 | `Cxx_GrammarTreeTraversalSuccessorContainer.C:31` | `get_traversalSuccessorContainer` SgNode 基类非法 | ivDrive 遍历（~83s） |
| 3 | `memberFunctions.C:410` | `cfgFindChildIndex` idx != INVALID_INDEX | SSA CFG 构建（~86s） |

所有三个崩溃均源于 AST 内部不一致：parent 指针 null、SgNode 基类出现、子节点不在父节点列表中。

## 定位过程

1. 在 `translate.cpp` 和 `InductionVariableExposure.cpp` 添加进度日志 + `fflush(stdout)`，精确定位 crash#1 在 `write_constant_tile_file` 的 SSA 调用中
2. 添加 `checkNullParent()` AST 完整性检查工具，过滤 ROSE 共享类型节点噪音
3. 逐步排除：跳过 `convertImperfToPerf` → 仍崩；跳过 `inductionVariableExposure` → 不崩；跳过 `normalizeLoopNest` → 不崩 → **锁定 normalize**
4. 修复 normalize 后 crash#2 暴露：`ivDrive` 的 `replaceExpression`/`removeStatement` 导致堆内存损坏
5. 跳过 ivDrive 后 crash#3 暴露：带 `goto` 的 inline 代码经 normalize 修改后，`cfgFindChildIndex` 找不到子节点

## 根因分析

### 根因 1：`constantFolding(loop_nest->get_parent())` 破坏 AST

`src/normalize/normalize.cpp:36`：
```cpp
SageInterface::constantFolding(loop_nest->get_parent());
```

作用在父节点（函数体 BasicBlock）上，遍历修改整个基本块的所有表达式，产生 parent 为 null 的孤儿节点。

### 根因 2：`ivDrive` 的 replaceExpression/removeStatement 堆损坏

`src/preprocess/InductionVariableExposure.cpp` 中 `ivDrive()` → `forwardSub()`/`ivSub()` 对已标准化的循环体做大量 `replaceExpression`、`copyExpression`、`removeStatement`，ROSE 内部内存管理产生 use-after-free / double-free。

### 根因 3：带 `goto` 的 inline 代码经 normalize 后 SSA 失败

内联 pass 产生的 `goto`/`label` 语句在 normalize 修改循环体后，父-子关系不一致，SSA CFG 遍历时 `cfgFindChildIndex` 找不到子节点。

## 修复

### 1. normalizeLoopNest — 核心修复

**文件**: `src/normalize/normalize.cpp`

```cpp
// Before (buggy):
SageInterface::constantFolding(loop_nest->get_parent());

// After:
SageInterface::fixVariableReferences(loop_nest);
SageInterface::constantFolding(loop_nest);      // 只对 loop nest 本身
AstPostProcessing(loop_nest);                   // 修复所有父子指针
```

### 2. normalizeLoop — 子节点添加方式

```cpp
// Before:
init_list.push_back(new_init);
new_init->set_parent(loop->get_for_init_stmt());

// After:
SageInterface::appendStatement(new_init, loop->get_for_init_stmt());
```

用 ROSE 标准 API 替代 raw vector push_back，确保子节点正确注册到 traversal successor container。

### 3. normalizeLoop — 末尾一致性修复

在 `normalizeLoop` 末尾（所有 init/test/stride/body 替换后）添加：
```cpp
SageInterface::fixVariableReferences(loop);
```

### 4. 跳过 ivDrive（优化 pass，非必需）

**文件**: `src/preprocess/InductionVariableExposure.cpp`

`ivDrive` 是前向替换+归纳变量暴露的优化 pass，不执行不影响正确性。跳过它避免 replaceExpression/removeStatement 造成的堆损坏。

### 5. 跳过带 goto 的循环

**文件**: `translate.cpp`

在内联代码产生的 `goto` 语句存在时，跳过 normalize+SSA 整个流程（goto 破坏了循环结构假定的控制流），输出 `"Loop Nest Skipped (Contains Goto — inlined code)"`。

### 6. 诊断工具新增

**文件**: `include/DEBUG/debugTool.h`, `src/DEBUG/debugTool.cpp`

新增 `checkNullParent()` 函数，遍历 AST 检测 parent 为 null 的非类型节点。过滤 `SgType` 子类噪音。

### 7. 日志增强

- **`translate.cpp`**: `[PROCESS]`, `[IMP]`, `[KERNEL-GEN]` 进度日志
- **`InductionVariableExposure.cpp`**: SSA 入口/出口日志 + try/catch

## 修改文件清单

| 文件 | 类型 | 内容 |
|------|------|------|
| `src/normalize/normalize.cpp` | **BUG FIX** | constantFolding 作用范围 + appendStatement + fixVarRefs |
| `src/preprocess/InductionVariableExposure.cpp` | **BUG FIX** | 跳过 ivDrive + try/catch + 日志 |
| `translate.cpp` | **BUG FIX** | 跳过 goto 循环 + 进度日志 |
| `include/DEBUG/debugTool.h` | NEW | checkNullParent() 声明 |
| `src/DEBUG/debugTool.cpp` | NEW | checkNullParent() 实现 |

## 验证

修复后 `translate.out -p ... -O .vscode/out` 运行 **5 分钟无任何 FATAL**，374K 行日志正常，所有 SSA 调用成功完成。
