# c6c —— 用 c6 写的 c6 编译器

自举、脱离 tcc、多后端（native x86-64 / C99 / LLVM IR）的 c6 编译器。

本目录是**独立项目边界**：自己的构建入口（CMake 目标 `c6c` 或自带 Makefile）、自己的许可证、不 `#include` `tcc/*.h`、
不链接 `libtcc`、不复制 `tcc/` 的源码（`make c6c-lint` 机器可查）。仓库里的 `tcc/` 只承担
两件事：**引导第一步**（用它编出 c6c stage1）和**差分测试的 oracle**。

| 文档 | 内容 |
|---|---|
| [docs/plan.md](docs/plan.md) | 目标、验收口径、实测基线、阶段计划、风险 |
| [docs/design.md](docs/design.md) | 架构、流式 IR（16 字节/条）、三后端、类型表位分配、镜像与落盘 |
| [docs/concurrency.md](docs/concurrency.md) | 协程 / 线程 / 原子 / TLS |
| [docs/modules.md](docs/modules.md) | 模块前缀、`import`、std/ 的 153 个模块 |
| [docs/todo.md](docs/todo.md) | 逐轮账本：做过什么、结论是什么、哪些是硬限制 |
| [docs/acceptance.md](docs/acceptance.md) | 四条验收的命令、实测数字、还差什么 |

## 现状：四条验收

一条命令（仓库根 `cmake --build` 会把 c6 和 c6c 一起编出来）：

```sh
cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build --parallel
cmake --build build --target check-c6c        # 验收 (1)(2)(3)（一条命令）
cmake --build build --target check-c6c-perf   # 再加上验收 (4)

# 等价的细粒度入口（CI 用这几个）：
ctest --test-dir build -L c6c -LE perf --output-on-failure   # 验收 (1)(2)(3)
ctest --test-dir build -L perf --output-on-failure           # 验收 (4) 性能/内存
```

不用 CMake 也行（c6c 自带 Makefile，两个入口等价）：

```sh
make -C c6c check-all ROOT=$PWD     # 验收 (1)(2)(3)
make -C c6c bench-c6c ROOT=$PWD     # 验收 (4)
make -C c6c tests2-check ROOT=$PWD  # 只跑 tcc 上游 tests2 的闸门
```

| # | 要求 | 现状 |
|---|---|---|
| 1 | 自举 fixpoint 逐字节、tests+std+诊断、三后端同一套用例 | ✅ fixpoint 逐字节相同；前端 123/0 + 跑通 67/0、std 377/377、诊断 314/314、C99 32/32、LLVM 29/29 |
| 2 | c6c 编出可用的 tcc，并由它跑通 tests2 与 selfhost | ✅ 那个 tcc 跑 tests2 118/144；**第三代 selfhost**（它再编一遍 tcc.c）出体 |
| 3 | c6c 直接跑 tcc 上游用例 | 通过 **112/144**，其余是 10 条架构性 n/a（每条写死了卡点）、8 条上游只编不跑、14 条上游 expect 过期；**意外失败 0** |
| 4 | native ≤135 ms、自编译 ≤100 ms、RSS ≤32 MB、线性度 ≤2.6x | ✅ **112 ms / 88 ms / 25 MB / 每行成本 0.39x** |

`make -C c6c lint` 仍然机器检查目录边界（不 `#include` `tcc/*.h`、不链接
`libtcc`、不复制 `tcc/` 源码）。

## 直接跑 c6 源码：`-run`

```sh
./c6c/build/c6c -run hello.c6          # 编到 $TMPDIR 的临时 ELF，exec 完就删
./c6c/build/c6c -run app.c6 a b        # 参数原样交给 main（argv[0] 是源文件名）
./c6c/build/c6c -run my_test.c6        # 只有 test 块的文件：合成入口，失败数当退出码
```

也能直接当脚本解释器用（内核把脚本路径排在 `-run` 后面）：

```c
#!/path/to/c6c/build/c6c -run
```

`-run` 走的是"落一份临时 ELF 再 `exec`"，不是 tcc 那种 mmap + 重定位的 JIT：
c6c 只产静态 ELF，`exec` 它没有 ld.so / PLT 的额外成本，也正好绕开 c6c
还没有动态链接这件事。退出码照抄程序（被信号杀按 shell 惯例 `128+sig`），
临时文件在子进程结束后删除，落在 `$TMPDIR`（默认 `/tmp`）。只支持 native
后端；`-o`、`-c`、`-E`、`-fsyntax-only`、`--emit-c`、`--emit-llvm` 都跟它互斥。
回归：`make -C c6c runflag`。

许可证：`LICENSE` 为 MIT，与 `tcc/`（LGPL-2.1）严格分离；若项目所有者另有安排，替换本文件即可。

## 跑 `test "…" { }` 块：`-test`

```sh
./c6c/build/c6c -test my_test.c6                 # 跑全部 test 块（用户 main 不执行）
./c6c/build/c6c -test -test-filter=切片 x.c6     # 只跑描述里含"切片"的块
./c6c/build/c6c -test -o x my_test.c6 && ./x     # 只写文件，之后自己跑
./c6c/build/c6c -test x.c6 tests/tu/x.c6         # 多 TU 的块一起登记
```

输出协议和 tcc 的 native 运行器（`tcc/tccrun.c` 的 `tcc_run_tests`）逐字一致，
所以两个编译器可以互相替换：每块一行 `PASS: 描述` / `FAIL: 描述: 错误消息`
（FAIL 行走 stderr），收尾一行 `N test(s), M failure(s)` 在 stdout 最后，
**失败数当退出码**（全过是 0）。一个块都没有、或 `-test-filter` 过滤空了时
静默退出 0。不给 `-o` 时编到 `$TMPDIR` 的临时 ELF 再 `exec`（和 `-run` 同一条
路径），给了 `-o` 就只写文件不跑。只支持 native 后端；`-c`、`-E`、
`-fsyntax-only`、`--emit-c`、`--emit-llvm`、`-nostdlib` 都跟它互斥。
回归：`make -C c6c testflag`。
