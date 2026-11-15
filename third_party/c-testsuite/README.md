# c-testsuite（vendored）

c6c 语言层 C99 兼容性的**外部闸门**。这里只是数据，跑法是 `c6c/tests/c99-suite.sh`
（或 `make -C c6c c99suite`）。

## 来源

| 项 | 值 |
|---|---|
| 上游仓库 | https://github.com/c-testsuite/c-testsuite |
| 上游提交 | `5c7275656d751de0e68b2d340a95b5681858ed07`（2020-03-09） |
| 套件入口 | `single-exec`（单文件、`main` 入口、stdout+stderr 必须等于 `*.c.expected`、程序退出码 0） |
| 本目录内容 | 只取 **scc 来源**的 150 条：`single-exec/NNNNN.c` + `.c.expected` + `.c.tags` + `.c.otags` |
| 许可证 | ISC（见 `LICENSE`；每条用例的出处/版本在 `*.c.otags`） |

**没有** vendoring 的 69 条来自 tinycc（LGPL-2.1）：它们和仓库里已有的
`tcc/tests/tests2` 是同一批文件，已经由 `c6c/tests/tcc-tests2.sh` 覆盖，重复引入
只会多一份许可证上的麻烦。另外 `00001.c`（`int main(){return 0;}`）没有 `.otags`，
出处不可考，也不收。

## 为什么是这个套件

* 它就是编译器圈子里的“公共 C 测试集”，用例是**单文件、纯 C99 语言特性**导向的
  （218/220 条 `portable`，43 条标 `c99`），不依赖构建系统、不需要多 TU。
* 接口极小：编一个可执行 → 跑 → 比 stdout+stderr。没有断言框架、没有网络、
  没有时间依赖，适合当一个可复现的闸门。
* 每条用例带 `.tags`（`c89`/`c99`/`c11`/`needs-cpp`/`needs-libc`/`portable`），
  所以“语言层”和“库层”的失败可以分开统计——这正是本项目要的口径：
  **语言层 100% 兼容 C99，库不管**。

## 跑法

```sh
make -C c6c c99suite ROOT=$PWD           # 记分板（有失败时退出码非 0）
C6C=build/c6c ./c6c/tests/c99-suite.sh -v # 逐条看分类
```

分类口径见 `c6c/tests/c99-suite.sh` 头部注释：`PASS` / `LANG`（前端编不过）/
`OUT`（跑出来的输出不符）/ `LIB`（链接期缺符号）/ `STALE`（上游 `.expected` 本身过期，
用 gcc 判定）/ `SKIP`（gcc 也编不过，说明用例不是合法 C99）。
