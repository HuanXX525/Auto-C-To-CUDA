# Auto-C-To-CUDA

> An automatic C to CUDA transcompiler built with the [ROSE Compiler](http://rosecompiler.org).  This system is capable of handling some `while` loops and some imperfectly nested `for` loops.  It makes use of loop fission and extended cycle shrinking to extract parallelism out of certain loops, if dependency tests allow for the transformations.  The system was created as a Master's Thesis project.  
>
> Forked from Automatic Transcompiler of Affine C Programs to CUDA By Leart Krasniqi

## 安装

[安装流程](./scripts/README.md)

流程中的ROSE可以直接安装[预编译的二进制文件](https://github.com/rose-compiler/rose/wiki/Install-Using-apt-get)或者[从源码编译](https://github.com/rose-compiler/rose/wiki/Installation-on-Ubuntu-From-Source)

## 配置文件说明

### config.json

- `safe_functions`：该项预填写了CUDA支持的所有C语言数学库函数；位于该项内的函数的函数调用将会被依赖测试模块直接跳过；适用于无法找到定义的纯函数
