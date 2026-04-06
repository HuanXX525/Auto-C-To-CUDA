# Pass 和 PassManager

> 照着llvm的pass的样子简单实现了一套Pass和PassManager，用于模块化/可插拔的写一些分析和变换；PM为PassManager的简称

## Pass

1. `PassContext`：本质上是一个string-any的一个`unordered_map`，使用字符串来做索引，用于在上下文之间传递信息

2. `Pass`整个`Pass`的基类，纯虚函数，定义了`run`、`initialize`、`finalize`接口，分用于跑pass，在运行 pass 前/后做初始化

3. `AnalysisPass`分析pass的基类，在AST上做分析，不修改

4. `TransformPass`变换pass的基类，用于修改AST

- pass的具体用法参考`transforms`路径下的代码（include里放`hpp`，src里放`cpp`）

## PassManager

- Pass的管道，通过顺序的往 `PassManger` 里塞 `Pass`，然后 PM 顺序执行 Pass
