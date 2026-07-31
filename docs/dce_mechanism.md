# 死代码消除（DCE）机制说明

> 本文直接描述当前 `src/preprocess/deadCodeElim.cpp` 的死代码消除机制：
> 它如何判定语句有用/无用、如何构建 def-use 边、以及如何保证"仅用作数组下标
> 的变量声明"不被误删。涉及代码：`src/preprocess/deadCodeElim.cpp`、
> `src/translate.cpp`（SSA 生命周期）、`src/normalize/normalize.cpp`
> （规范化，不做声明保存-恢复）。

## 1. 流水线位置

```
collect + normalize（normalizeLoopNest，src/normalize/normalize.cpp）
    ->  fixForLoopTests
    ->  SSA（ROSE StaticSingleAssignment，project 级，normalization 之后构建）
    ->  inductionVariableExposure
    ->  重建 SSA
    ->  eliminateDeadCode（每个循环巢的最外层循环体 block，translate.cpp:514）
    ->  affine / dependency / codeGen（生成 .cu）
```

关键约定：

- **SSA 在 normalization 之后构建**（`translate.cpp` Phase 1/Phase 2）：规范化会
  改写循环（`for (i = L; i <= U; i += S)` 形态）并把体内索引替换为
  `1*i + -1LL` 形式的表达式，改写后的节点必须纳入 SSA 的 def-use 表，
  否则新语句查不到定义者。
- `normalizeLoopNest()` 只调用 `SageInterface::AstPostProcessing(loop_nest)`
  修复父指针——**不运行任何 DCE，也不做声明的保存-恢复**（实测确认
  `AstPostProcessing` 不删除声明，见 `.vscode/test/rose_dce_repro`）。
- `eliminateDeadCode` 作用于每个循环巢的**最外层循环体 block**；嵌套循环体
  内部的语句不在扫描范围内。

## 2. 判定与删除流程

`eliminateDeadCode(block, ssa)` 分四步：

### 2.1 语句展开（flattenStmts）

把 `block` 内的语句展平成程序序列表 `allStmts`：遇 `for`/`while`/`do-while`
整体入列（不再深入），遇嵌套 `SgBasicBlock` 递归展开。

### 2.2 构建 def-use 边

- **defMap**：`VarName → 定义语句列表`。每条语句通过 `getDefinedVar` /
  `collectDefs` 提取其定义的变量：
  - 赋值 `a = ...`、复合赋值 `a += ...`、`++a`/`a--`、变量声明
    `SgVariableDeclaration`（单变量）均算定义；
  - 同一变量可有多个定义语句（声明 + 若干次赋值），全部入列。
- **preds**：对每条语句 `s`，从其 SSA 使用信息收集前驱。使用集合来自
  `ssa->getUsesAtNode`，查询节点包括语句本身、语句内所有
  `V_SgFunctionCallExp` 与 `V_SgVarRefExp` 子节点。

### 2.3 标记有用语句并反向传播

- `isUsefulStmt(s)` 判定"绝对有用"（有可见效果或控制流）：
  - 控制流：`if`/`for`/`while`/`do-while`/`goto`/`switch`/`break`/`continue`/
    `return`；
  - 副作用：含函数调用、对数组 LHS 写入（`hasArrayWrite`）；
  - 累加器：复合赋值（`+=` 等）与 `++`/`--`——其最终值在循环外可能被读，
    视为副作用；
  - 其余语句（裸赋值、声明）不作为根，需靠 def-use 边被传播标记。
- 以所有 useful 语句为种子做 worklist 反向传播：语句 X 有用 → X 的所有
  前驱（定义了 X 所用变量的语句）也标记为有用。

### 2.4 删除未标记的定义

逆序遍历 `allStmts`，删除"未标记有用且定义了变量"的语句
（`getDefinedVar(s) != nullptr`）。

## 3. 防止误删"仅作数组下标"的声明

ROSE SSA 的 `getUsesAtNode` 有一个已知盲区：**赋值语句 LHS 的数组下标索引
不被记录为 SSA 使用**。若声明仅被 `out[idx] = ...;` 这类语句的下标引用，
纯靠 SSA 使用信息将找不到其任何使用 → 判定为死代码。当前实现用两条
互补机制闭合这个盲区：

1. **defMap 查找剥离 SSA 版本后缀**：`getUsesAtNode` 返回的
   `VarName` 形如 `[声明, 版本号]`，而 `defMap` 的键是 `[声明]`（无版本）。
   查找前只取 `vn[0]`（声明指针），保证版本不同的 SSA 使用也能命中定义者。
2. **AST 回退扫描**：对每条 `isUsefulStmt` 为真的语句，直接遍历其
   `V_SgVarRefExp` 子节点，把每个引用的声明在 `defMap` 中查找并加入
   `preds[s]`——完全绕开 SSA 的使用记录，因此 `out[idx] = ...;` 中的
   `idx` 一定能连到其声明语句。`idx` 的声明经反向传播被标记为有用而保留。

**效果**（复现实验见 `.vscode/test/rose_dce_repro/README.md`）：修复前
的 DCE 会误删 6 种"仅作 LHS 数组下标使用"的声明模式（gcc 报
`undeclared`）；当前实现全部保留，产物可正常编译。

## 4. 设计要点

- **为什么用"声明指针"作集合成员**：`SgInitializedName*` 指针等价，
  声明/使用/定义在 ROSE AST 中是同一对象，指针可直接作为 def-use 的键。
- **为什么只删 `getDefinedVar` 的语句**：删除范围限定为"定义语句"
  （赋值/声明/累加），不删除表达式语句或控制流语句，避免破坏循环结构。
- **为什么查 `isUsefulStmt` 语句的 AST 引用**：数组写（`hasArrayWrite`）是
  "有用"的根节点，其下标变量正是 SSA 盲区所在，回退扫描只在这些语句上
  做，成本低、覆盖面正好。
- **为什么不在 normalize 阶段做声明保存-恢复**：`AstPostProcessing` 不运行
  DCE，声明不会在那里丢失；DCE 盲区已由第 3 节两条机制闭合。历史上曾在
  normalize 中"保存-恢复"声明（把循环巢内引用但未在巢内声明的变量复制到
  循环体顶部），实测会导致函数级变量被复制进循环体造成**声明遮蔽**
  （`matrix3_multiply` 返回全零矩阵、向量噪声参数丢失），现已移除。

## 5. 验证结果

Aircraft-Simu 全量重新翻译（64 个 `.cu`）、构建、重跑 `baseline_dev_001`
（2 实例，与 C 原版同种子同配置）：

| 指标 | CUDA 翻译版 | C 基线 |
|------|------------|--------|
| max_dcm_orthogonality_error | 2.11426607047e-15 | 2.11426607047e-15 |
| model_degradation_flags_or | 2 | 2 |
| max_quat_norm_error | 2.220446049250e-16 | 2.220446049250e-16 |
| min_miss_distance | 0.349539 | 0.349539 |

- 每个实例的 `trajectory.csv`、`trajectory_diagnostics.csv`、`summary.json`、
  `run_manifest.json`、`event_log.txt`、`sensor_log.bin`、`command_log.bin`、
  `fc_internal_log.bin` 与 C 基线**逐字节一致**；
- `campaign_summary.json` 与 C 基线仅 `*_wall_time_s`（运行耗时）不同，
  其余字段逐位相同；
- 产物中不再存在遮蔽副本（含 `fopen`/`fork`/`parse_args` 等有副作用调用的
  重复执行）；回归测试（common/environment/flight_control/instance_manager/
  batch_runner/closed_loop）行为与 C 原版一致。
