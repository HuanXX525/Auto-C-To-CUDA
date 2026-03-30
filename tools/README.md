# tools 目录

> 用来放一些帮助开发的测试代码/小工具。

## 目录结构

```
tools/
├── Makefile          # 顶层 Makefile，负责从项目总 Makefile 传递编译参数，并扫描所有子项目
├── Makefile.temp     # 子项目 Makefile 模板
├── astdump/          # 子项目：AST dump 工具
├── playground/       # 子项目：开发测试用
└── README.md
```

## 构建机制

`tools/` 下的每个子目录是一个独立的子项目，各自编译为一个可执行程序。

构建流程如下：

1. **顶层 `tools/Makefile`** — 扫描 `tools/` 下所有含 `Makefile` 的子目录，将项目总 Makefile 的编译参数传递给各子项目。
2. **子项目 `Makefile`**（如 `playground/Makefile`）— 负责实际编译：扫描子目录下所有 `.cpp` 文件，编译并链接。
3. **产物输出** — 生成的二进制文件统一放到 `build/bin/tools/` 目录下。

## 新建 tool

1. 在 `tools/` 下新建一个目录，将 `Makefile.temp` 复制进去并重命名为 `Makefile`。
2. 编辑 `Makefile`，将 `TARGET` 设置为你的工具名称。
3. 编写 `.cpp` 源码。**注意：该目录下所有 `.cpp` 中必须且只能有一个 `main` 函数。**
