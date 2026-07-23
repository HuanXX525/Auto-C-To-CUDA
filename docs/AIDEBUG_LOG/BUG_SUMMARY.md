# translate.out 运行时错误总结

项目 AIDEBUG_LOG 中记录了 4 份调试报告，涵盖 6 个独立 bug。按根因归纳为以下三类：

---

## 一、AST 节点管理不当（4 个 bug）

**核心问题**：ROSE AST 的父子关系（parent 指针与 child list）不一致，或 parent 为 null，导致 SSA/CFG 遍历断言失败。

### Bug 1: `constantFolding(loop_nest->get_parent())` 破坏兄弟节点

| 项目 | 内容 |
|------|------|
| 崩溃点 | `memberFunctions.C:174` — `getNodeJustAfterInContainer`: parent == null |
| 根因 | `src/normalize/normalize.cpp:36` 在父节点（函数体 BasicBlock）上做 `constantFolding`，修改了整个块的表达式，产生 parent 为 null 的孤儿节点 |
| 修复 | 改为 `constantFolding(loop_nest)` + `AstPostProcessing(loop_nest)`，只对 loop nest 本身操作 |
| 来源 | `FIX_normalize_ast_corruption.md` |

### Bug 2: `push_back` 绕过 ROSE 子节点注册

| 项目 | 内容 |
|------|------|
| 崩溃点 | 同上 |
| 根因 | `src/normalize/normalize.cpp:74-75` 使用原始 `init_list.push_back(new_init)` + `set_parent` 替代 `SageInterface::appendStatement`，绕过 ROSE 的 traversal successor container 注册 |
| 修复 | 改用 `SageInterface::appendStatement(new_init, loop->get_for_init_stmt())` |
| 来源 | `FIX_normalize_ast_corruption.md` |

### Bug 3: 多次非完美循环转换导致父子不一致

| 项目 | 内容 |
|------|------|
| 崩溃点 | `memberFunctions.C:410` — `cfgFindChildIndex`: idx == INVALID_INDEX |
| 根因 | `convertImperfToPerf` 的第二次调用在已修改过的 AST 上执行 `replace_statement`，产生 child 的 `get_parent()` 与父节点 child list 不匹配 |
| 修复 | 每个函数限 1 次非完美循环转换 (`impConvCount`) |
| 来源 | `DEBUG_ANALYSIS_REPORT.md` |

### Bug 4: ivDrive 的 replaceExpression/removeStatement 堆损坏

| 项目 | 内容 |
|------|------|
| 崩溃点 | `Cxx_GrammarTreeTraversalSuccessorContainer.C:31` — SgNode 基类非法遍历 / `malloc` heap corruption |
| 根因 | `src/preprocess/InductionVariableExposure.cpp` 的 `ivDrive` → `forwardSub`/`ivSub` 对已标准化的循环体做大量 `replaceExpression`、`copyExpression`、`removeStatement`，ROSE 内部内存管理产生 use-after-free |
| 修复 | 跳过 `ivDrive`（优化 pass，不执行不影响正确性，只影响生成的代码表达形式） |
| 来源 | `FIX_normalize_ast_corruption.md` |

---

## 二、SSA 分析范围问题（1 个 bug）

**核心问题**：SSA 扫描了整个 project（含 kernel 函数），遇到不满足断言的变量作用域。

### Bug 5: kernel 函数被 SSA 分析导致断言失败

| 项目 | 内容 |
|------|------|
| 崩溃点 | `staticSingleAssignmentCalculation.C:803` — `insertDefsForExternalVariables` scope 检查失败 |
| 根因 | `kernelFnDef` 创建的 CUDA kernel 函数插入全局作用域后，后续循环的 `inductionVariableExposure` 对整个 project 运行 SSA，SSA 处理 kernel 函数时遇到 `thread_x_id` 等局部变量，其作用域不在允许列表中 |
| 修复 | `kernel_fn->get_file_info()->setCompilerGenerated()` — 标记为编译器生成，SSA 的 FunctionFilter 自动跳过 |
| 注意 | 必须在**声明节点**（`kernel_fn`）上设置，不能在定义节点（`get_definition()`）上 |
| 来源 | `DEBUG_REPORT.md`, `DEBUG_ANALYSIS_REPORT.md` |

---

## 三、分析逻辑缺陷（1 个 bug）

**核心问题**：循环体分析逻辑有漏洞，导致语义错误的 kernel 代码被生成（虽然是正确性 bug 而非崩溃）。

### Bug 6: forwardSub 跳过自引用归纳变量

| 项目 | 内容 |
|------|------|
| 错误行为 | 输入 `t = t + 1; arr[t] = t;`，生成的 kernel 中所有线程都用 `t=10`，输出 `arr[11]=11` × N 次（正确应为 `arr[11]=11, arr[12]=12, ...`） |
| 根因 | `src/preprocess/InductionVariableExposure.cpp:196` — `forwardSub` 在遍历 RHS 的变量引用时，遇到 `var == defVar`（自引用）直接 `continue` 跳过，未检查其 reaching definition 是否来自循环内部；导致错误的 return false，ivSub 未被调用 |
| 修复 | 在 for 循环后、Step 2 前添加自引用检查：若 `defVar` 在 `usedVars` 中且其 reaching definition 来自循环内部，则 return true 触发 ivSub |
| 来源 | `analysis_report.md` |

---

## 总表

| # | 类别 | Bug | 崩溃/错误 | 修复方式 | 报告文件 |
|---|------|-----|-----------|---------|---------|
| 1 | AST 一致 | constantFolding 作用范围 | parent == null | 缩小作用范围 + AstPostProcessing | FIX_normalize |
| 2 | AST 一致 | push_back 绕过注册 | parent == null | appendStatement | FIX_normalize |
| 3 | AST 一致 | 多次 imperf 转换 | cfgFindChildIndex | 限制每函数 1 次 | DEBUG_ANALYSIS |
| 4 | AST 一致 | ivDrive 堆损坏 | SgNode 基类错误 | 跳过 ivDrive | FIX_normalize |
| 5 | SSA 范围 | kernel 函数被扫描 | insertDefs scope | setCompilerGenerated | DEBUG_REPORT |
| 6 | 分析逻辑 | forwardSub 自引用 | 语义错误 | 补充自引用检查 | analysis_report |

## 经验教训

1. **ROSE AST 操作必须使用标准 API**：`appendStatement` 而非 `push_back`，确保 traversal successor container 同步
2. **不能随意扩大操作范围**：`constantFolding(parent)` 会污染兄弟节点
3. **SSA 需要隔离生成代码**：`setCompilerGenerated()` 标记 kernel 函数
4. **复杂 inline 代码（goto）不适合归一化/并行化**：跳过即可，不影响其他循环
5. **ivDrive 是可选优化**：遇到堆稳定性问题时可以跳过，不影响正确性
