# DCE 误删内联参数副本声明修复报告

## 问题描述

翻译 Aircraft-Simu 项目的 `tools/map_preprocess/src/main.c` 后，nvcc 编译报错：

```
/workspace/output/Aircraft-Simu/tools/map_preprocess/src/main.cu(940):
error: identifier "text__461_inline_240" is undefined
    while(text__461_inline_240 != 0 && (*text__461_inline_240) != '\0'){
```

## 触发场景

`lowercase_key` 函数被内联到 `read_esri_ascii_header` 的 `for(field...)` 循环体中：

```c
// 原始调用
lowercase_key(key);

// 内联后 ROSE 生成（对应 main.cu:939-946）
{
    char *text__461_inline_240 = key;       // ← 参数副本声明——被 DCE 误删
    while (text__461_inline_240 != 0 && *text__461_inline_240 != '\0') {
        *text__461_inline_240 = tolower(*text__461_inline_240);
        ++text__461_inline_240;
    }
    rose_inline_end__462: ;
}
```

ROSE 的 `doInline` 在内联时将函数的参数 `text` 重命名为 `text__461_inline_240`，并生成了一行声明 `char *text__461_inline_240 = key;`。由于 `text` 在函数体内被修改（`++text`），ROSE 不能直接将实参 `key` 替换入所有引用点，必须保留局部副本。

## 根因分析

罪魁祸首是 `src/preprocess/deadCodeElim.cpp` 的 DCE（死代码消除）pass。声明 `char *text__461_inline_240 = key;` 被 Step 4 当作死代码删除。

DCE 的 def-use 边构建逻辑中（Step 1），代码对每个变量引用 `SgVarRefExp` 调用 `ssa->getReachingDefsAtNode_(ref)` 获取精确的到达定义。在这个场景下：

1. `text__461_inline_240` 在 `while` 循环中既读又问——属于"循环自改变量"
2. SSA 在 while 的 header 处为该变量创建了 **phi 节点**（合并入口 def 和回边 def）
3. 循环体内的 `++text__461_inline_240` def 被扁平化跳过（`flattenStmts` 在 while 边界停止，while 内部的 stmt 不在 `allStmts` 集合里）
4. 旧版本的 DCE（`935c929`）使用 **defMap 保守策略**：任何同变量引用→最近块内定义，内联声明总能保持连接
5. `bfb2b99` 的重写将 defMap 替换为 per-ref reaching-defs 查询，并加入 **"跳过 phi 节点"** 的保守策略，导致该声明的 def-use 链完全断裂 → 0 条入边 → 被删除

### 关键问题（二重 bug）

此外，`foundUsable` 标记被跨变量污染。`getReachingDefsAtNode_` 返回的是整张 reaching-def 表（包含**所有变量**的达到定义），而 `foundUsable` 是按引用来判定的全局标记。当 `text__461_inline_240` 的本变量达到定义是 phi 被跳过时，表中 `key`、`file` 等其他变量的条目仍将 `foundUsable` 置为 true，从而**完全跳过了对本变量的回退逻辑**。

## 修复方案

修改文件：`src/preprocess/deadCodeElim.cpp`

### 1. lastDef 回退机制（核心修复）

在 Step 1 的 def-use 边构建中，维护一个随程序顺序逐条推进的 `lastDef` 映射（VarName → 最近的块内定义 stmt）。对每个变量引用：

- **优先使用精确边**：若 SSA reaching def 非 null、非 phi、且 def 的 enclosingStmt 在 `allStmts` 中 → 建边
- **无守卫地添加回退边**：从 `lastDef` 中查找该变量最近的块内定义 → 建边

不再依赖 `foundUsable` 作守卫，改为直接总是添加 lastDef 边（preds 是 `set`，天然去重）。这样既保留了 SSA 精确边的跨迭代精确性，又在循环自改变量场景下恢复了必要的保守连接。

**原问题（935c929 的 defMap）**：同变量多次定义时后一次 def 覆盖前一次，导致之前的使用都指向最后的 def（已由 bfb2b99 解决——lastDef 按程序顺序增量更新，引用只会指向最近的前驱 def，不会受后续 def 影响）。

### 2. 指针解引用写检测（防御加固）

`isUsefulStmt` / `hasArrayWrite` 只覆盖 `SgPntrArrRefExp`（数组访问 `a[i]`），不识别指针解引用写 `*p = ...`。添加 `hasPointerDerefWrite` 辅助函数检测 `SgPointerDerefExp` 作为赋值 LHS 的场景，并将其加入 `isUsefulStmt` 的内存副作用判断。内联函数中常见的 `*p = value` 模式因此不再完全依赖 def-use 边保命。

## 验证

### 1. 单文件翻译（debug 日志确认）

```
[DCE-REMOVED] char *text__156_inline_79 = key;    // 修复前 → 被删除
Dead code elimination removed 1 definitions

// 修复后 → 无 DCE-REMOVED 日志，声明保留
```

### 2. 全量翻译 + nvcc 编译

```bash
./build/bin/translate.out -p external/Aircraft-Simu/build/compile_commands.json \
  -O output/Aircraft-Simu
make -C output/Aircraft-Simu/build -j3
```

**结果：`[100%] Built target`，全部 .cu 编译通过**，不再有 `text__461_inline_240 undefined` 错误。

### 3. 正确性测试

```bash
make test_run FORCE=1
```

**结果：All Test Passed**

## 变更文件

| 文件 | 变更 |
|------|------|
| `src/preprocess/deadCodeElim.cpp` | Step 1: 添加 `lastDef` 映射和回退边逻辑；添加 `hasPointerDerefWrite` 及 isUsefulStmt 集成 |

## 相关提交

| 提交 | 说明 |
|------|------|
| `935c929` | 首次修复 DCE 误删内联声明（defMap 方案） |
| `bfb2b99` | 将 defMap 替换为 per-ref reaching-defs，引入 phi-skip 回归 |
| **本次修复** | 在 reaching-defs 基础上添加 lastDef 回退，恢复保守连通性 |
