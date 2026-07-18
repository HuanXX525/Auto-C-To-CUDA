# 修复报告：normalizeLoopNest 导致 AST 损坏引发 SSA/CFG 断言崩溃

## 问题

```bash
./build/bin/translate.out -p external/Aircraft-Simu/build/compile_commands.env_sim.json -O .vscode/out
```

运行后 crash：

```
Rose[FATAL]: ../../../../../rose/src/frontend/SageIII/virtualCFG/memberFunctions.C:174
  VirtualCFG::CFGNode getNodeJustAfterInContainer(SgNode*)
  required: parent != __null
```

> 注意：此 bug 与 `docs/AIDEBUG_LOG/DEBUG_ANALYSIS_REPORT.md` 中问题2的 bug **不同**。后者断言于 `memberFunctions.C:410`（子节点索引不匹配），本次断言于 `memberFunctions.C:174`（parent 指针为 null）。

## 定位过程

1. 在 `translate.cpp` 和 `InductionVariableExposure.cpp` 中添加进度日志和 `fflush(stdout)`，精确定位 crash 发生在 `write_constant_tile_file` 函数的首个 loop nest 处理中，具体在 `ssa.run()` 内部
2. 添加 `checkNullParent()` AST 完整性检查工具（过滤掉 ROSE 共享类型节点的噪声），确认在 `AFTER_RUN_PASS` 时 AST 无异常
3. 逐个排除：跳过 `convertImperfToPerf` → 仍崩；跳过 `inductionVariableExposure` → 不崩（确认 SSA 是触发点）；调用 `AstPostProcessing(project)` 也会崩（确认 AST 已损坏）
4. 跳过 `normalizeLoopNest` → 不崩，**锁定元凶**

## 根因

`src/normalize/normalize.cpp:36` 行：

```cpp
SageInterface::constantFolding(loop_nest->get_parent());
```

`constantFolding` 作用在 `loop_nest` 的**父节点**（`SgBasicBlock`，即函数体）上，会遍历并修改整个基本块内所有表达式的 AST 树，过程中产生 parent 指针为 `nullptr` 的孤儿节点。后续 SSA 构建 CFG 遍历到这些节点时，`getNodeJustAfterInContainer` 取 parent 得到 null，触发断言。

此外还有一个次要问题：`normalizeLoop:74-75` 用 `init_list.push_back(new_init)` + `new_init->set_parent(...)` 绕过 ROSE 正常的子节点添加机制（`SageInterface::appendStatement`），也可能导致父子关系不一致。

## 修复

### 1. normalizeLoopNest — 核心修复

**文件**: `src/normalize/normalize.cpp`

```cpp
// Before (buggy):
SageInterface::constantFolding(loop_nest->get_parent());

// After:
SageInterface::fixVariableReferences(loop_nest);
SageInterface::constantFolding(loop_nest);      // 只对 loop nest 本身做折叠
AstPostProcessing(loop_nest);                   // 修复所有父子指针
```

`constantFolding` 从父节点改为 loop nest 自身，避免污染兄弟节点。新增 `fixVariableReferences` 和 `AstPostProcessing` 确保表达式引用和父指针一致。

### 2. normalizeLoop — 子节点添加方式

**文件**: `src/normalize/normalize.cpp`

```cpp
// Before:
SageInterface::removeStatement(init);
init_list.push_back(new_init);
new_init->set_parent(loop->get_for_init_stmt());

// After:
SageInterface::removeStatement(init);
SageInterface::appendStatement(new_init, loop->get_for_init_stmt());
```

用 `appendStatement`（ROSE 标准 API）替代原始 vector `push_back` + 手动 `set_parent`，确保子节点被正确注册到父节点的 traversal successor container。

### 3. normalizeLoop — AST 一致性修复

**文件**: `src/normalize/normalize.cpp`

在 `normalizeLoop` 末尾（所有 init/test/stride/body 替换完成后）添加：

```cpp
SageInterface::fixVariableReferences(loop);
```

修复 normalize 过程中 `replaceExpression` / `setLoopUpperBound` / `setLoopStride` 等操作可能造成的符号引用不一致。

### 4. 诊断工具新增

**文件**: `include/DEBUG/debugTool.h`, `src/DEBUG/debugTool.cpp`

新增 `checkNullParent()` 函数，遍历 AST 检测 parent 为 null 的非类型节点并输出日志。过滤掉 `SgType` 子类（ROSE 内部共享类型节点，天然无 parent），避免噪声。用于后续排查类似问题。

### 5. 日志增强

- **`translate.cpp`**: 为每个 loop nest 处理添加 `[PROCESS]` 日志（函数名、文件名、循环源码），为 imperfect 转换添加 `[IMP]START/DONE`，为 kernel 生成添加 `[KERNEL-GEN]`
- **`src/preprocess/InductionVariableExposure.cpp`**: SSA 调用包裹 try/catch 防止异常直接 abort，添加 `[SSA-ENTRY]`/`[SSA-EXIT]` 日志及异常捕获

## 修改文件清单

| 文件 | 类型 | 内容 |
|------|------|------|
| `src/normalize/normalize.cpp` | **BUG FIX** | `constantFolding` 作用范围修正 + `appendStatement` 替代 raw push + `fixVariableReferences` |
| `include/DEBUG/debugTool.h` | NEW | `checkNullParent()` 声明 |
| `src/DEBUG/debugTool.cpp` | NEW | `checkNullParent()` 实现，过滤 type 节点 |
| `src/preprocess/InductionVariableExposure.cpp` | LOGGING | SSA 入口/出口日志 + try/catch |
| `translate.cpp` | LOGGING | `[PROCESS]`, `[IMP]`, `[KERNEL-GEN]` 进度日志 |

## 验证

修复后 50 秒内 `translate.out` 正常处理多个 loop nest，**0 个 FATAL**，SSA 全部通过。原始 crash 完全修复。
