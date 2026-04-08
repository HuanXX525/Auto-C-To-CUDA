# translate\_all.sh

- translate\_all.sh 脚本的用法

```
Usage: translate_all.sh [translate|benchmark] [options]

Commands:
  translate          翻译.c为.cu (默认)
  benchmark          编译.c和.cu并用hyperfine对比性能

Options:
  -d DIR             目标目录 (默认: .)
  -o, --report F     benchmark报告文件 (默认: build_report/benchmark_report_{DIR}.md)
  --cc CMD           C编译器 (默认: gcc)
  --nvcc CMD         CUDA编译器 (默认: nvcc)
  --cflags FLAGS     C编译选项 (默认: -O2 -lm)
  --nvccflags FLAGS  CUDA编译选项 (默认: -O2)
  --warmup N         hyperfine预热次数 (默认: 3)
  --runs N           hyperfine运行次数 (默认: 10)
```

- 常用用法：

```
# 查看帮助
bash translate_all.sh -h
# 翻译
bash translate_all.sh translate -d /tmp/benchmarks
# 测试加速
bash translate_all.sh benchmark -d /tmp/benchmarks
```
