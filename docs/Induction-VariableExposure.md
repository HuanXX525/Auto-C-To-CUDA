# 4\.5 归纳变量暴露（Induction\-Variable Exposure）

死代码消除与常量传播都依赖于**定义 \- 使用图**中相对简单的模式。归纳变量替换则更为复杂，需要识别相当复杂的模式。因此，归纳变量替换通常需要一套成熟的程序分析框架。

鉴于该框架的存在，且归纳变量替换的核心需求源于**将常规编程写法转化为更便于依赖测试的形式**，归纳变量替换阶段的功能往往不止于替换辅助归纳变量。一个常见的附加功能是：**将区域不变表达式前向替换到数组下标中**。

本节将介绍**前向表达式替换**与**循环归纳变量替换**算法，并最终说明二者如何协同工作。

## 4\.5\.1 前向表达式替换（Forward Expression Substitution）

以下示例可直观体现前向替换的价值：

```fortran
DO I = 1, 100
    K = I+2
    A(K) = A(K) + 5
ENDDO
```

程序员已手动完成公共子表达式消除，将两次下标计算合并为变量`K`。但依赖测试工具难以分析数组`A`的访问依赖 —— 因为下标中的`K`**不是归纳变量**，却在循环内变化。

对`K`执行前向替换后：

```fortran
DO I = 1, 100
    A(I+2) = A(I+2) + 5
ENDDO
```

新形式可直接用于依赖测试；若面向向量机编译，生成代码效率也会显著提升。

- 原代码：即便可向量化，也需将`K`扩展为临时向量，作为`A`的**分散 \- 聚集索引**，开销大。

- 替换后：直接生成简洁的向量运算指令。

### 执行前提

归纳变量替换与前向表达式替换，不仅依赖**定义 \- 使用边**，还需**控制流分析**：

1. 定义归纳变量的语句：**循环每次迭代都必须执行**。

2. 前向替换的定义：必须保证**在被替换语句之前、每次循环迭代中都执行**。

借助 \\*\\SSA 图（静态单赋值图）\\\\* 可高效判断上述性质，核心是先确定语句是否属于目标循环：

- 为每个语句维护数据结构，记录其嵌套的循环集合。

- 若语句`S`的嵌套循环包含`L`，则`S`属于循环`L`；通过**循环层级**快速匹配即可。

### 算法逻辑

前向替换仅处理**仅含循环不变量 \+ 循环归纳变量**的表达式，SSA 图可快速验证该条件：

1. 遍历候选语句`S`的所有 SSA 入边：

    - 边来自循环内语句：该语句必须是**循环入口处归纳变量的 φ 节点**；否则含循环变化量，禁止替换。

    - 边来自循环外：表达式为循环不变量，允许替换。

2. 特殊情况：循环内先赋值**循环不变量**，再使用，仍可替换（按语句顺序处理，先替换不变量赋值，再处理后续表达式）。

示例：

```fortran
DO I = 1, N
S1: IC = IB ! IB为循环不变量
S2: IX = IC + 5
ENDDO
```

先将`IC=IB`前向替换到`S2`，再处理`IX`，最终`IX=IB\+5`（循环不变）。

### 循环展开与归纳变量更新

DO 循环展开后，归纳变量更新在**循环末尾**（所有使用之后）：

```fortran
! 原循环
DO I = 1, N
ENDDO

! 展开后
L: IF (I > N) GO TO E
    I = 1
L1: I = I+1
    GO TO L
E:
```

因此，归纳变量的所有使用，都必然关联**循环入口处合并归纳变量值的 φ 节点**。

### 前向替换算法（伪代码）

```Plain Text
boolean procedure ForwardSub(S, L)
// S：候选替换语句；L：目标循环
// 依赖SSA图、语句嵌套信息
// 返回true：因S含循环变化输入，放弃前向替换，尝试归纳变量替换
// 功能：替换L中所有仅含L的归纳变量/不变量的语句

if S是φ函数 或 S有副作用 then
    return false

// 第一步：判断输入是否循环不变
for each SSA入边e of S do
    Ss = 边e的源语句
    if Ss在循环L内 且 Ss定义的变量≠L的归纳变量 then
        return true  // 含循环变化量，尝试归纳变量替换

// 第二步：执行替换
all_uses_gone = true
all_loop_uses_gone = true

for each SSA出边e of S do
    St = 边e的目标语句
    if St在循环L内 且 St不是φ节点 then
        // 替换：用S的右值，替换St中所有S左值的出现
        将St中S左值替换为S的右值
        // 更新SSA边
        for each SSA入边ie of S的右值 do
            删除边e
            新增边：ie的源 → St
    else
        all_uses_gone = false
        if St不在L内 then
            all_loop_uses_gone = false

// 第三步：处理原语句S
if all_uses_gone and all_loop_uses_gone then
    删除语句S
else if all_loop_uses_gone then
    将S移到循环外

return false
end ForwardSub
```

### 关键要求

SSA 图需支持**双向遍历**（入边、出边），否则算法无法执行。返回值用于驱动流程：`true`表示放弃前向替换，需尝试归纳变量替换。

## 4\.5\.2 归纳变量替换（Induction\-Variable Substitution）

前向替换完成后，核心任务是**识别辅助归纳变量**并替换。

### 定义 4\.5：辅助归纳变量

对于 DO 循环`DO I=LB, UB, S`，**辅助归纳变量**是指：在循环内所有使用位置`L`，均可表示为：
$cexpr \times I + iexpr_L$
其中：

- `cexpr`：循环不变量；

- `iexpr\_L`：循环不变量（不同位置可不同）。

**最简形式**：辅助归纳变量由以下语句定义：
$K = K \pm cexpr$
（`cexpr`为循环不变量）

### 复杂归纳变量示例

```fortran
DO I=1, N
    J=K+1
    K=J+2
ENDDO
```

`J`和`K`均为该循环的辅助归纳变量。

### 归纳变量识别条件

语句`S`定义辅助归纳变量，需满足：

1. **SSA 循环条件**：`S`属于 SSA 图中的**简单环**，环内仅含`S`\+**1 个循环头部 φ 节点**（合并循环内外归纳变量值）；若含其他 φ 节点，则非归纳变量（无法每次迭代更新）。

2. **表达式形式**：更新式为`K=K±cexpr`，`cexpr`循环不变。

示例（含 SSA 图）：

```fortran
DO I=1, N
    A(I) = B(K)+1
    K = K+4
    D(K) = D(K)+A(I)
ENDDO
```

SSA 图中`K`的定义 \- 使用形成**单 φ 节点环**，是辅助归纳变量的典型特征。

### 归纳变量识别算法（伪代码）

```Plain Text
boolean procedure isIV(S, iV, cexpr, cexpr_edges)
// S：候选语句；iV：输出归纳变量；cexpr：输出增量；cexpr_edges：增量的SSA入边
// 返回true：S定义归纳变量

is_iv = false

// 条件1：S属于循环内仅含自身+φ节点的SSA环
if S在循环内 且 S属于仅含自身+φ节点的SSA环 then
    iV = S的左值变量
    // 提取增量：右值移除iV后剩余部分
    temp_rhs = S的右值 - iV
    if iV在右值中以加法形式出现 then
        loop_invariant = true
        cexpr_edges = ∅
        // 检查增量是否循环不变
        for each SSA入边e of temp_rhs do
            if e的源在当前循环内 then
                loop_invariant = false
            else
                cexpr_edges = cexpr_edges ∪ {e}
        // 处理减法：转为加法（取反）
        if temp_rhs是减法 then
            cexpr = -temp_rhs
        else
            cexpr = temp_rhs
        if loop_invariant then
            is_iv = true

return is_iv
end isIV
```

### 归纳变量替换算法（伪代码）

```Plain Text
procedure IVSub(S, L)
// S：候选归纳变量语句；L：目标循环
// 功能：识别并替换辅助归纳变量

// 第一步：识别归纳变量
if not isIV(S, iV, cexpr, cexpr_edges) then
    return

// 第二步：获取循环信息
innermost_L = 包含S的最内层循环  // DO I=L, U, S
Sh = 循环头部iV的φ节点
So = φ节点Sh的**唯一外部源**（SSA性质）

// 第三步：替换循环内iV的使用
// 3.1：替换S**之前**的语句：迭代数-1
for each SSA出边e of Sh （指向同循环内节点） do
    St = e的目标语句
    更新SSA边(e, cexpr_edges, So)
    将St中iV替换为：iV + ((I-L)/S)*cexpr

// 3.2：替换S**之后**的语句：迭代数
for each SSA出边e of S （指向同循环内节点） do
    St = e的目标语句
    更新SSA边(e, cexpr_edges, So)
    将St中iV替换为：iV + ((I-L+S)/S)*cexpr

// 第四步：处理原语句S
if S有**循环外出边** then
    // 移到循环外，更新增量为最终值
    将S移到循环外
    新增边：So → S
    删除边：Sh → S
    S的增量改为：((U-L+S)/S)*cexpr
else
    // 无外部使用：删除S及相关φ节点
    删除语句S
    删除边：Sh → S
    删除φ节点Sh
    删除边：So → Sh

return
end IVSub
```

### SSA 边更新辅助函数

```Plain Text
procedure update_SSA_edges(e, cexpr_edges, So)
// e：替换边；cexpr_edges：增量入边；So：iV外部定义点

for each 边ie ∈ cexpr_edges do
    新增边：So → e的目标
    新增边：ie的源 → e的目标
删除边e

return
end update_SSA_edges
```

### 替换规则

辅助归纳变量的替换公式分两类：

- **S 之前的语句**：`iV \+ \(\(I\-L\)/S\)\*cexpr`（迭代数 \- 1）

- **S 之后的语句**：`iV \+ \(\(I\-L\+S\)/S\)\*cexpr`（迭代数）

（`I`= 循环索引，`L`= 下界，`U`= 上界，`S`= 步长）

## 4\.5\.3 替换流程驱动（Driving the Substitution Process）

前向替换与归纳变量替换需按**固定顺序**协同执行，核心原则：

1. **前向替换优先**：先处理前向替换，可能生成新归纳变量。

2. **归纳变量替换：由内到外**：内层循环替换可能生成外层新归纳变量，需从最内层循环向外处理。

### 驱动算法（伪代码）

```Plain Text
procedure IVDrive(L)
// L：目标循环；依赖SSA图
// 功能：递归执行前向替换+归纳变量替换

foreach 语句S in L （按原始顺序） do
    switch S的类型
        case 赋值语句：
            FS_not_done = ForwardSub(S, L)
            if FS_not_done then
                IVSub(S, L)
        case DO循环：
            IVDrive(S)  // 递归处理内层循环
        default：
            无操作
end switch

return
end IVDrive
```

### 与循环规范化的交互

**归纳变量替换前，必须先做循环规范化**，否则生成低效代码：

#### 未规范化循环（低效）

```fortran
DO I=L, U, S
    K = K+N
    ... = A(K)
ENDDO

! 替换后（含循环内整数除法/乘法，依赖测试失败）
DO I=L, U, S
    ... = A(K + (I-L+S)/S * N)
ENDDO
K = K + (U-L+S)/S * N
```

#### 规范化后循环（高效）

```fortran
! 先规范化：步长=1
I = 1
DO i=1, (U-L+S)/S, 1
    K = K+N
    ... = A(K)
ENDDO
I = I+1

! 替换后（仅1次乘法，强度削减可消除，依赖测试支持）
DO i=1, (U-L+S)/S, 1
    ... = A(K + i*N)
ENDDO
K = K + (U-L+S)/S * N
I = I + (U-L+S)/S
```

### 复杂归纳变量扩展

多更新的归纳变量（如`K=K\+1`连续两次）：

```fortran
DO I=1, N, 2
    K=K+1
    A(K)=A(K)+1
    K=K+1
    A(K)=A(K)+1
ENDDO
```

- **识别**：定义语句形成**单 φ 节点 SSA 环**，满足加减不变量规则。

- **替换**：为每个定义语句分配独立替换公式；或增强前向替换，传播不变表达式，简化为标准归纳变量。

## 4\.6 本章小结

本章介绍了支撑**依赖测试**的核心变换，将数组下标转为标准形式：

1. **循环规范化**：将循环转为`下界1→上界、步长1`，简化依赖测试（存在一定缺陷）。

2. **常量传播**：编译期用常量替换未知变量，基于数据流图算法实现。

3. **归纳变量替换**：消除辅助归纳变量，替换为标准循环归纳变量的线性函数；支持循环嵌套中的表达式折叠。

上述变换依赖**数据流分析**：迭代求解数据流方程、构建**使用 \- 定义链**或**SSA 形式**。
