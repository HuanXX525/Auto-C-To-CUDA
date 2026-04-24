**tests/run_compare.sh 脚本**

用于一键转换、编译指定文件夹下的c程序，并将c与cu的结果进行一一对比输出

1. C -> CU 转换
2. 分别编译 C 和 CUDA
3. 运行并对比标准输出
4. 输出 PASS/FAIL 汇总和日志目录
5. 日志和中间产物会放到 /tmp/c2cuda_compare_时间戳/
6. 在上述文件夹下对应的fall程序名下的diff.log查看输出结果

运行命令

```
tests/run_compare.sh -d <目标目录>
```
