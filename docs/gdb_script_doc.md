# gdb script

- 调试命令比较繁琐，要输c代码的路径，输cu输出的路径，中间还夹着个 `-rose:o`，方便调试就写个 `xxx.gdb` （`scripts/gdb_script/common.gdb` 就是简单的设置了 `translate.out` 后面跟的命令行参数，测试用的代码 `tmp/example2.c` 就是 `tests/example.c`，`tmp`目录是被 gitignore的）

- 用法：
```
gdb -x /path/to/gdb_script/xxx.gdb /path/to/bin/translate.out

# 然后就是打断点、运行等等调试步骤
```
