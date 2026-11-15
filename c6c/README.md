# c6c —— 用 c6 写的 c6 编译器

自举、脱离 tcc、多后端（native x86-64 / C99 / LLVM IR）的 c6 编译器。

本目录是**独立项目边界**：自己的构建入口（CMake 目标 `c6c` 或自带 Makefile）、自己的许可证、不 `#include` `tcc/*.h`、
不链接 `libtcc`、不复制 `tcc/` 的源码（`make c6c-lint` 机器可查）。仓库里的 `tcc/` 只承担
两件事：**引导第一步**（用它编出 c6c stage1）和**差分测试的 oracle**。

这两件事里的第一件**已经可以不用 tcc**：c6c 的源码是 C99 兼容的单 TU
（`src/c6c.c6` 把各模块 `#include` 进来），所以 stage0 可以换成宿主 C 编译器：

```sh
make -C c6c build BOOTSTRAP=host HOSTCC=gcc     # 只要一个 C 编译器
cmake -S . -B build -DC6_ENABLE_TCC=OFF         # 连 tcc 都不构建
```

`tcc/` 剩下的是"语料 + oracle"这一件事：`make build-tcc` / `tests1` / `tests2`
仍然编它的源码来量 c6c 的深浅，但**不需要 tcc 的产品构建**（不经过 CMake 的
tcc 目标、不安装、不出 `libtcc`）。其中 `build-tcc` 要的 `tcc/libtcc1.a` 是
运行时，必须由能用的 tcc 编（宿主 cc 编的等价物会让成绩从 118/144 掉到
102/144）；`tcc/c6` 缺席时 Makefile 用宿主 cc 把 `tcc/tcc.c` 编一个**参考
tcc** 当夹具，再由它编运行时——不安装、不进交付物。CI 的 `no-tcc` 作业就是
这条路的机器证据（构建完断言 `tcc/c6` 不存在，再跑 `ctest -L c6c`）。

| 文档 | 内容 |
|---|---|
| [docs/plan.md](docs/plan.md) | 目标、验收口径、实测基线、阶段计划、风险 |
| [docs/design.md](docs/design.md) | 架构、流式 IR（24 字节/条）、三后端、类型表位分配、镜像与落盘 |
| [docs/concurrency.md](docs/concurrency.md) | 协程 / 线程 / 原子 / TLS |
| [docs/modules.md](docs/modules.md) | 模块前缀、`import`、std/ 的 153 个模块 |
| [docs/todo.md](docs/todo.md) | 逐轮账本：做过什么、结论是什么、哪些是硬限制 |
| [docs/acceptance.md](docs/acceptance.md) | 四条验收的命令、实测数字、还差什么 |

## 现状：四条验收

一条命令（仓库根 `cmake --build` 会把 c6 和 c6c 一起编出来）：

```sh
cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build --parallel
cmake --build build --target fast             # c6c 当编译器的快速门禁（≈40 s，含外部语料）
cmake --build build --target check-all-c6c    # c6c 当编译器的全量门禁（≈4 min，含外部语料）
cmake --build build --target check-c6c        # 验收 (1)(2)(3)（同上，走 ctest）
cmake --build build --target check-c6c-perf   # 再加上验收 (4)

# 等价的细粒度入口（CI 用这几个）：
ctest --test-dir build -L c6c -LE perf --output-on-failure   # 验收 (1)(2)(3)
ctest --test-dir build -L perf --output-on-failure           # 验收 (4) 性能/内存
```

不用 CMake 也行（c6c 自带 Makefile，入口等价）：

```sh
make -C c6c fast ROOT=$PWD          # 快速门禁（≈40 s）
make -C c6c check-all ROOT=$PWD     # 验收 (1)(2)(3)
make -C c6c bench-c6c ROOT=$PWD     # 验收 (4)
make -C c6c tests2-check ROOT=$PWD  # 只跑 tcc 上游 tests2 的闸门
```

`fast` 与 `check-all` 的口径一致——被测编译器就是 c6c 自己——只是 `fast` 把
`selfhost`/`selftest`/`stdtest`/`test`/`diagtest`/`c99test`/`build-tcc` 这几条
全量编译挪出去；第三方语料（`c99suite`、上游 `tests1`/`tests2`）两条都包含。

| # | 要求 | 现状 |
|---|---|---|
| 1 | 自举 fixpoint 逐字节、tests+std+诊断、三后端同一套用例 | ✅ fixpoint 逐字节相同；前端 123/0 + 跑通 67/0、std 377/377、诊断 314/314、C99 32/32、LLVM 29/29 |
| 2 | c6c 编出可用的 tcc，并由它跑通 tests2 与 selfhost | ✅ 那个 tcc 跑 tests2 118/144；**第三代 selfhost**（它再编一遍 tcc.c）出体 |
| 3 | c6c 直接跑 tcc 上游用例 | 通过 **115/144**，其余是 7 条架构性 n/a（每条写死了卡点）、8 条上游只编不跑、14 条上游 expect 过期；**意外失败 0** |
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

## 出目标文件：`-c`

`-c` 只编不链，输出**可重定位目标文件**（ET_REL `.o`）：段表、符号表、
`.rela.text` / `.rela.data` 都按 System V x86-64 ABI 走，所以系统的链接器
能直接接住它——一条源文件一个 `.o`，跨 TU 的全局变量和函数按名字互认。

```sh
./c6c/build/c6c -c lib.c6  -o lib.o        # 一个 TU 一个 .o
./c6c/build/c6c -c main.c6 -o main.o
cc main.o lib.o -o prog && ./prog          # 交给系统链接器
```

口径细节：

* 外部函数调用落成 `R_X86_64_PLT32` + `UND` 符号（和 gcc 一样，动态链接时
  才进 PLT），段内/本 TU 已定义的调用落成 `R_X86_64_PC32`；
* 数据段里指向别处的地址（`&f`、`p = "x"`、`&&label`）是 `R_X86_64_64`，
  不是拍死的常量——`.o` 里没有绝对地址；
* TU 私有的名字（字符串字面量 `.LC<n>`、`static` 的 `helper$2`、编译器自己的
  TLS 记账格 `__c6_tls_*`）是 `LOCAL`，两个 `.o` 不会撞车；
* `extern int counter;` 在 `-c` 下**只是引用**（C99 6.9.2p2），不分配存储；
* `-c` 不出 `_start`、不带运行时、不查 `main`，一次只编一个源文件。

还没有的：c6c 自己的链接器（现在得借系统 `cc` 的）、`.bss`（零初始化数据也
落在 `.data` 里）、weak / 可见性 / 段 GC、`-fPIC` 与 `-shared`。
回归：`make -C c6c objcheck`（单/多 TU 真编、真链、真跑 + readelf 结构核对）。

## 链接：`c6c -o prog a.o b.o`

输入里出现 `.o` 就走 c6c **自己的链接器**（`src/ld.c6`），不再借系统的 `cc`：

```sh
./c6c/build/c6c -c lib.c6  -o lib.o
./c6c/build/c6c -c main.c6 -o main.o
./c6c/build/c6c -o prog main.o lib.o && ./prog     # 静态链接，产物 ET_EXEC
```

它做什么：

* 读 ELF64 `ET_REL`（c6c 自己出的为主，`cc -c` 出的简单 C 目标文件也吃），
  按段名归并 `.text` / `.rodata` / `.data` / `.bss` / `.init_array` /
  `.fini_array`，其余（`.comment`、`.note*`、`.eh_frame`、调试段）丢掉；
* 符号解析：本地符号各管各的 `.o`，全局按名字，强定义盖弱定义，两个强定义
  报错；未定义符号默认**硬报错**（退出码 2，名字进 stderr）；
* 重定位：`R_X86_64_64` / `PC32` / `PLT32` / `32` / `32S`，越界报错不截断；
* 合成 `_start`：取 `argc/argv/envp` → 跑 `.init_array` → 调 `main` →
  跑 `.fini_array` → `syscall 60` 退出（退出码 = `main` 的返回值）；
* 产物：两个 `PT_LOAD`（代码 R+E / 数据 R+W）+ 不可执行的 `PT_GNU_STACK`。

口径与限制（都是刻意的，写在这儿免得被当成 bug）：

* **静态**链接。要调 libc 就得接 libc.so.6（`DT_NEEDED` + PLT/GOT + 动态
  loader），那是下一步；现在给带 libc 名字的 `.o`（比如自带运行时 `rt.o`
  里 `environ`、`openat` 那些）用 `-Wl,--allow-undefined`：未定义符号按 0
  处理并打一条汇总警告——只要运行路径不碰它就能跑。
* 多 TU 的 **TLS** 不合并：c6c 的 TLS 偏移按 TU 分配，跨 `.o` 会重叠。
  恰好一个 `.o` 带 TLS 时，链接器会把它的 `__c6_tls_img/len/delta` 抄给
  其它 `.o`（构造函数在运行时那侧，它得知道有 TLS 块要建）；两个以上就
  如实报错。
* 不做：`.a` 静态库、`-shared`/`-fPIC`、段 GC、符号版本、调试信息重定位。

回归：`make -C c6c ldcheck`（纯自足两个 `.o` / 真实规模的 `rt.o` 564 条
重定位 / 构造函数顺序 / 跨 `.o` TLS / 缺符号报错 / `readelf` 结构核对）。

## 库形态：把 c6c 嵌进别人的程序

`src/embed.c6` 把编译器包成一套 API（`include/c6lib.h`），同一份源码用
`-DC6C_NO_MAIN` 关掉 `main()` 就能编成库：

```sh
make -C c6c libc6c            # → c6c/build/libc6c.a + libc6c.so
```

```c
#include <c6lib.h>

C6State *s = c6_new();
c6_compile_string(s, "int printf(const char*,...);"
                     "int main(void){ printf(\"hi\\n\"); return 42; }");
if (c6_run(s, 0, NULL) != 42)          /* 编进内存，就地跑 */
    fprintf(stderr, "%s\n", c6_error(s));
void *m = c6_get_symbol(s, "main");    /* 取符号地址（必要时先 relocate） */
c6_output_file(s, "prog");             /* 或者落盘成可执行文件 */
c6_delete(s);
```

API 一览：`c6_new` / `c6_delete` / `c6_error` / `c6_add_include_path` /
`c6_set_runtime_path` / `c6_define_symbol` / `c6_compile_file` /
`c6_compile_string` / `c6_relocate` / `c6_get_symbol` / `c6_run` /
`c6_output_file`。库形态没有 `argv[0]` 可推，所以 `lib/rt.c6` 和自带
`include/` 的绝对路径在编译期就塞进去（`C6_DEFAULT_RT` / `C6_DEFAULT_INC`）。

**`tcc_*` 兼容层**：`include/tcc-compat.h` 声明的是 tcc 那套名字
（`tcc_new` / `tcc_compile_string` / `tcc_relocate` / `tcc_run` /
`tcc_get_symbol` / `tcc_output_file` / `tcc_set_options`），实现一一接到
`c6_*` 上。原本用 tcc 嵌入库的程序，把包含路径和链接库换掉就能编过、跑出
一样的结果——这是"顶替 tcc 产品位"里最实的一步。

口径与限制：一个 `C6State` 只能编一次（第二次如实拒绝，不做静默错结果）；
`tcc_set_error_func` 收了回调但诊断仍走 stderr；`tcc_add_file` 只收
`.c6`/`.c` 源文件，不收 `.o`/`.a`；`tcc_set_options` 只认 `-I`/`-D`/`-U`。

回归：`make -C c6c embedcheck`（6 条：动态库 / 静态库 / 落盘后独立跑 /
编译失败被报告 / 二次编译被拒绝 / `tcc_*` 兼容层）。

## 安装与包契约：没有 tcc 也能装出全套

`cmake -DC6_ENABLE_TCC=OFF` 时安装清单按 c6c 自己的搜索规则来定落点：

```
bin/c6                                  编译器（c6c 编的）
include/c6lib.h  include/libtcc.h       宿主 C 程序用的头（兼容层装成 libtcc.h）
lib/libc6c.a     lib/libc6c.so          嵌入库
lib/c6/rt.c6     lib/c6/include/*.h     编译器自带的运行时与头文件
lib/cmake/c6/c6Config.cmake             find_package(c6)
lib/pkgconfig/{c6,c6lib}.pc             pkgconf
```

c6c 用 `argv[0]` 推自己的目录去找 `<bin>/../lib/c6/{include,rt.c6}`，所以
整棵树搬走、换前缀都还能自己找到自带头文件。

两种用法，`C6_ENABLE_TCC=OFF` 下都成立：

```cmake
find_package(c6 0.1 REQUIRED COMPONENTS libtcc)   # 没有 tcc 时由 libc6c 顶上
target_link_libraries(app PRIVATE c6::libtcc)     # 也可以直接用 c6::c6lib
```

```sh
cc $(pkgconf --cflags --libs c6lib) host.c -o host   # → -lc6c
```

注意 `include/` 里只放宿主用的两个头：c6 自己的 `stdio.h`/`stddef.h` 在
`lib/c6/include/`，装到系统 includedir 会把宿主程序的系统头文件盖掉。

回归：`make -C c6c pkgcheck`（7 条：无 tcc 也能 configure / 安装树齐全 /
`include/` 干净 / `find_package` 消费者真编真跑 / `pkgconf` 旗子
`-Wall -Werror` 过得去 / 搬到别的前缀的 `c6` 自找头文件与运行时）。
