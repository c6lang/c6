# c6c —— 用 c6 写的 c6 编译器

[![CI](https://github.com/c6lang/c6/actions/workflows/ci.yml/badge.svg)](https://github.com/c6lang/c6/actions/workflows/ci.yml)
[![GitHub release](https://img.shields.io/github/v/release/c6lang/c6)](https://github.com/c6lang/c6/releases)
[![License](https://img.shields.io/github/license/c6lang/c6)](https://github.com/c6lang/c6)
[![AUR package](https://repology.org/badge/version-for-repo/aur/c6.svg)](https://repology.org/project/c6/versions)

自举、脱离 tcc、多后端（native x86-64 / C99 / LLVM IR）的 c6 编译器。

本目录是**独立项目边界**：自己的构建入口（CMake target `c6c`，没有自己的 Makefile 了）、
自己的许可证、不 `#include` `tcc/*.h`、不链接 `libtcc`、不复制 `tcc/` 的源码
（`cmake --build build --target lint` 机器可查）。仓库里的 `tcc/` 只承担
两件事：**引导第一步**（用它编出 c6c stage1）和**差分测试的 oracle**。

这两件事里的第一件**已经可以不用 tcc**：c6c 的源码是 C99 兼容的单 TU
（`src/c6c.c6` 把各模块 `#include` 进来），所以 stage0 可以换成宿主 C 编译器：

```sh
cmake -S . -B build -DC6_ENABLE_TCC=OFF && cmake --build build --target c6c
# 只要一个宿主 C 编译器：连 tcc 都不构建（默认，C6_ENABLE_TCC=OFF）
# 显式 -DC6_ENABLE_TCC=ON 时 stage0 才是 tcc/c6
```

`tcc/` 剩下的是"语料 + oracle"这一件事：`build-tcc` / `tests1-check` /
`tests2-check` 仍然编它的源码来量 c6c 的深浅，但**不需要 tcc 的产品构建**
（不经过 CMake 的 tcc 目标、不安装、不出 `libtcc`）。其中 `build-tcc` 要的
`tcc/libtcc1.a` 是运行时，必须由能用的 tcc 编（宿主 cc 编的等价物会让成绩从
118/144 掉到 102/144）；`tcc/c6` 缺席时 `c6c-tcc-corpus` 这个 target 用宿主 cc
把 `tcc/tcc.c` 编一个**参考 tcc** 当夹具，再由它编运行时——不安装、不进交付物。
CI 的 `no-tcc` 作业就是这条路的机器证据（构建完断言 `tcc/c6` 不存在，再跑
`ctest -L c6c`）。

| 文档 | 内容 |
|---|---|
| [docs/management.md](docs/management.md) | 工程管理规范：清单 / 打包 / 固件 / lock / workspace |
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

细到单条闸门也是 target（以前 `make -C c6c <目标>` 的那些名字，现在都是 CMake
target；`cmake --build build --target help` 能列全）：

```sh
cmake --build build --target fast            # 快速门禁（≈40 s）
cmake --build build --target check-all-c6c   # 验收 (1)(2)(3)
cmake --build build --target bench-c6c       # 验收 (4)
cmake --build build --target tests2-check    # 只跑 tcc 上游 tests2 的闸门
cmake --build build --target selfhost        # 只跑自举 fixpoint
cmake --build build --target objcheck        # 只跑 -c 目标文件的回归
```

| 原来的 `make -C c6c` 目标 | 现在的 CMake target |
|---|---|
| `build` / `libc6c` | `c6c` / `c6c-lib` |
| `test` / `ctest` | `test-c6c` / `ctest-c6c`（`test` 是 CMake 内建 target） |
| `fast` / `check-all` / `check-all-perf` | `fast` / `check-all-c6c` / `check-all-perf` |
| 其余（`lint unit irtest hello runflag testflag objcheck ldcheck dyncheck bcheck dbgcheck asmcheck a64check archcheck embedcheck pkgcheck selfhost selftest stdtest diagtest test-c6c c99test c99suite llvmtest tcc-tests tests1-check tests2-check build-tcc g2c-check scoreboard scan bench bench-c6c`） | 同名 |

`fast` 与 `check-all` 的口径一致——被测编译器就是 c6c 自己——只是 `fast` 把
`selfhost`/`selftest`/`stdtest`/`test`/`diagtest`/`c99test`/`build-tcc` 这几条
全量编译挪出去；第三方语料（`c99suite`、上游 `tests1`/`tests2`）两条都包含。

| # | 要求 | 现状 |
|---|---|---|
| 1 | 自举 fixpoint 逐字节、tests+std+诊断、三后端同一套用例 | ✅ fixpoint 逐字节相同；前端 52/0 + c6c 回归 92/0、std 381/381、诊断 316/316、C99 51/51、LLVM 51/0 |
| 2 | c6c 编出可用的 tcc，并由它跑通 tests2 与 selfhost | ✅ 那个 tcc 跑 tests2 118/144；**第三代 selfhost**（它再编一遍 tcc.c）出体 |
| 3 | c6c 直接跑 tcc 上游用例 | 通过 **115/144**，其余是 7 条架构性 n/a（每条写死了卡点）、8 条上游只编不跑、14 条上游 expect 过期；**意外失败 0** |
| 4 | native ≤135 ms、自编译 ≤100 ms、RSS ≤32 MB、线性度 ≤2.6x | ✅ **112 ms / 88 ms / 25 MB / 每行成本 0.39x** |

`cmake --build build --target lint` 仍然机器检查目录边界（不 `#include` `tcc/*.h`、不链接
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
回归：`cmake --build build --target runflag`。

许可证：仓库根的 `LICENSE` 现在就是本文件的 MIT；与 `tcc/`（LGPL-2.1，见 `tcc/COPYING`）严格分离。

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
回归：`cmake --build build --target testflag`。

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

还没有的：`.bss`（零初始化数据也落在 `.data` 里）、weak / 可见性 / 段 GC。
（`-fPIC` 收下即可——native 后端出来的代码本来就位置无关；`-shared` 见下面
「共享库」那节。）
回归：`cmake --build build --target objcheck`（单/多 TU 真编、真链、真跑 + readelf 结构核对）。

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

* 默认**静态**链接：给带 libc 名字的 `.o`（比如自带运行时 `rt.o` 里
  `environ`、`openat` 那些）要么加 `-Wl,--dynamic` 走动态链接，要么用
  `-Wl,--allow-undefined` 把未定义符号按 0 处理（打一条汇总警告，只要运行
  路径不碰它就能跑）。
* 多 TU 的 **TLS** 不合并：c6c 的 TLS 偏移按 TU 分配，跨 `.o` 会重叠。
  恰好一个 `.o` 带 TLS 时，链接器会把它的 `__c6_tls_img/len/delta` 抄给
  其它 `.o`（构造函数在运行时那侧，它得知道有 TLS 块要建）；两个以上就
  如实报错。
* 不做：`.a` 静态库、段 GC、符号版本、调试信息重定位（`-shared` 出共享库
  在下一节）。

回归：`cmake --build build --target ldcheck`（纯自足两个 `.o` / 真实规模的 `rt.o` 564 条
重定位 / 构造函数顺序 / 跨 `.o` TLS / 缺符号报错 / `readelf` 结构核对）。

## 动态链接：`c6c -Wl,--dynamic -o prog a.o rt.o`

加 `-Wl,--dynamic`，链接器就出一个**动态可执行文件**，把 libc 交给 ld.so：

```sh
./c6c/build/c6c -c app.c6 -o app.o
./c6c/build/c6c -Wl,--dynamic -o app app.o rt.o
./app                 # app.c6 里调 printf/getpid/getcwd…… 都真的走 libc
```

产物里有：

* `PT_INTERP` → `/lib64/ld-linux-x86-64.so.2`；
* `.dynstr` / `.dynsym` / `.hash`（SysV 哈希）——导入符号的名字与表项；
* `.rela.plt` + `.plt` + `.got.plt`——每个导入函数一条 `R_X86_64_JUMP_SLOT`；
* `.dynamic`——`DT_NEEDED libc.so.6`、`DT_STRTAB`/`DT_SYMTAB`、`DT_PLTGOT`、
  `DT_JMPREL`，外加 `DT_FLAGS = BIND_NOW`（非惰性绑定：ld.so 启动时把
  JUMP_SLOT 全解完，PLT 桩子就只剩一条 `jmp [rip+got]`）。

所以 `rt.o` 里那 22 个 libc 函数（`vsprintf`、`openat`、`feof`……）不用再
`--allow-undefined` 了——它们真从 libc.so.6 解析，`getpid()` 返回的 pid 就是
真 pid。

数据符号走 **COPY 重定位**：`extern int optind;`、`extern char **environ;`
这类在非 PIC 代码里是绝对地址，链接器在 `.bss` 里划一块、发一条
`R_X86_64_COPY`（进 `.rela.dyn`，配 `DT_RELA`/`DT_RELASZ`），ld.so 启动时把
libc 里那份定义拷过来——大小是从 libc.so.6 的 `.dynsym` 现查的（`optind` 4
字节、`environ` 8 字节），不是一律当 8 猜。

口径与限制：

* **libc 的启动初始化没做**：本程序的 `_start` 直接调 `main`，不经过
  `__libc_start_main`。所以"libc 启动时才填"的全局量（`environ`、`stdout`
  的缓冲区之类）拷进来仍是 0；纯函数的 libc 调用（`printf`/`getpid`/
  `malloc`/`strlen`……）不受影响。要用环境变量就自己读 `/proc/self/environ`
  或 `argv`（`rt.c6` 里就是这么干的）。
* 不做符号版本（`.gnu.version`）；`.rela.dyn` 里的 `R_X86_64_RELATIVE` 留给
  共享库（本程序是固定基址的 ET_EXEC，用不上）。
* 默认仍是静态链接：动态是**显式**选项（`-Wl,--dynamic`），拼错的链接器
  选项会直接报错，不会被当成告警开关静默忽略。

回归：`cmake --build build --target dyncheck`（14 条：不加 `--allow-undefined` 也能链、链接器
自报导入个数、数据符号点名警告、产物真跑且 libc 返回值对、`readelf` 逐项核对
ET_EXEC / 两个 `PT_LOAD` / `PT_INTERP` / `PT_DYNAMIC` / `DT_NEEDED` /
`BIND_NOW` / JUMP_SLOT 条数、`ldd` 认得 libc、默认静态仍会对未定义符号报错）。

## 共享库：`c6c -shared a.o -o liba.so`

```sh
./c6c/build/c6c -c lib.c6 -o lib.o
./c6c/build/c6c -shared lib.o -o liba.so
./c6c/build/c6c -shared -o libb.so b.o c.o     # 多个 .o 合成一个 .so
```

产物是 `ET_DYN`：

* **基址 0**：`st_value` 和段地址都是段内偏移，加载基址由 ld.so 选
  （所以同一个 `.so` 可以在不同进程里落到不同地址）；
* **没有 `PT_INTERP`、没有入口**（`e_entry = 0`）：它不自己起进程，是被
  `dlopen` 起来或者被别的产物链进去的；`_start` 那套合成代码也不生成；
* `.dynsym` 里除了导入的函数，还**导出**这个库里定义的全局/弱符号
  （函数 `T`、数据 `D`），`dlopen` + `dlsym` 就是查这张表；
* 代码本来位置无关（全局地址走 `lea rip+rel32` + 重定位），所以数据里剩下的
  绝对地址（字符串指针、初始化器、`.init_array` 里的函数指针）全进 `.rela.dyn`
  当 `R_X86_64_RELATIVE`，ld.so 加载时统一加基址；
* `DT_SONAME` 就是输出文件名（别的产物按这个名字记 `DT_NEEDED`），
  `DT_INIT_ARRAY`/`DT_FINI_ARRAY` 让 `__attribute__((constructor))` 在
  `dlopen` 那一刻就跑；
* 末尾补一张**节头表**（`.text` / `.dynsym` / `.dynstr` / `.hash` /
  `.rela.dyn` / `.dynamic` / `.shstrtab`……）：ld.so 不看它，但 GNU ld 把
  `.so` 当**输入**时只认节头（没有就 `file in wrong format`），
  `readelf --dyn-syms` / `nm -D` 也一样。

口径与限制：

* 输入**必须**是 `.o`（先 `-c` 再 `-shared`）：`-shared lib.c6` 会直接报错，
  不猜；
* 外部**数据**符号（`extern int optind;` 那种）如实拒绝——共享库里的外部数据
  要走 GOT 间接寻址，这一版还没做（COPY 重定位只适合固定基址的可执行文件）；
  外部**函数**没问题，走 PLT；
* `-fPIC` / `-fpic` 收下不报错（native 后端本来就位置无关），不加 `-shared`
  时同一个 `.o` 仍然出 `ET_EXEC`——共享库是**显式**选项；
* 不做符号版本、`.a` 静态库、`-Wl,-soname=` 之类的别名控制。

回归：`cmake --build build --target sharedcheck`（19 条：`ET_DYN` / 入口 0 /
无 `PT_INTERP` / 程序头 4 个 / `DT_SONAME` / `DT_INIT_ARRAY` / `RELATIVE` /
节头表齐全 / 导出符号 `T`+`D` / `dlopen`+`dlsym` 真调通且数据读写一致 /
字符串指针经 `RELATIVE` 后指向正确 / 构造器在 `dlopen` 时跑 / `cc -L … -l`
直接链也能跑 / 源文件与外部数据符号两条边界如实报错 / `-fPIC` 收下且默认
仍是 `ET_EXEC`）。

## 汇编文本：`-S`

`-S` 出 **x86-64 汇编文本**（GNU as 的 Intel 语法），不写 ELF：

```sh
./c6c/build/c6c -S -nostdlib prog.c6 -o prog.s
cc -x assembler -nostdlib -static -o prog prog.s && ./prog
```

* 从 IR 直接生成（`src/asmout.c6`），和 native 后端**同一套约定**：每个 vreg 一个
  栈槽（`[rbp-16*(n+1)]`）、二元运算 a→rax / b→rcx、比较结果物化成 0/1、调用走
  SysV 寄存器、`IR_RET` 里 `leave/ret`。所以两条路出的是同一串语义，只是这边交给
  as 去编码；
* 文件是**自足的**：`.intel_syntax noprefix` 开头，带 `_start`（`argc/argv` →
  `main` → `exit`）、数据段用 `.byte` 原样写出并按偏移插符号标签，末尾还有
  `.note.GNU-stack`；
* 符号名里的 `$`（TU 私有名的后缀）会换成 `.`——gas 的符号名收不了 `$`，所有
  引用都走同一个改名函数，所以是自洽的。

范围（v1）：整数/指针的运算、控制流、调用、递归、结构体、全局数据都能出文本；
**浮点、变参、内联汇编、TLS 还没写**，遇到会点名报错（`c6c: -S 暂不支持 i2f
（函数 …）`）并**拒绝写文件**——不做"静默产出一份错的 .s"。也就是说现在还不能
用 `-S` 编自带运行时（`rt.c6`）的程序，要用 `-nostdlib`。

回归：`cmake --build build --target asmcheck`（12 条：循环/调用/数组/递归/结构体/switch/全局量/
位运算八个程序都是"c6c → .s → 系统 as → 真跑"，退出码与 native 后端逐一对齐；
`.intel_syntax` / `_start` / C 名符号都在；没实现的 op 会拒绝写文件）。

## 调试信息：`-g`

`-g` 配 `-c` 出 .o，里面带一套 DWARF（v4 子集）：**函数名、函数地址范围、
指令地址→源码行**。用系统 `cc` 链成可执行文件就能直接调试：

```sh
./c6c/build/c6c -g -c prog.c6 -o prog.o
cc -g -o prog prog.o                     # 或者 cc -g -o prog prog.o rt.o
gdb prog -ex 'break main' -ex run -ex bt
# → Breakpoint 1, main () at prog.c6:10
addr2line -e prog -f 0x…                 # → main prog.c6:10
```

写进去的段：`.debug_abbrev` / `.debug_info` / `.debug_line` / `.debug_str`，
外加 `.rela.debug_info` / `.rela.debug_line`——.o 里的地址是 **`.text` 段内
偏移**，配 `R_X86_64_64` 重定位让链接器补成最终地址（这也是为什么地址字段在
文件里写 0）。

* 粒度是**语句**：表达式里跨行不影响，按语句停下就够了；
* 只有 DIE 是"编译单元 + 子程序"，**没有变量/类型信息**（gdb 里能按函数和行
  停、能 backtrace、能看源码，`info locals` 是空的）；
* 只对 `-c` 的产物生效：`-o`（c6c 自己链的可执行文件）没有段表，也就没带
  调试信息——要调试就走 `-c` + 系统 cc；
* 不加 `-g` 时目标文件里一个 `.debug_*` 段都没有（默认行为不变）。

回归：`cmake --build build --target dbgcheck`（9 条：六个段齐全、地址字段带重定位、行号表覆盖
源码每一行、DIE 里有 add/main、系统 cc 链得上跑得出、`addr2line` 与 `gdb` 都
认得出 `dbg.c6:10`、不加 `-g` 时没有调试段）。

## 边界检查：`-b`

加 `-b`，数组下标在取址之前先过一道检查：

```sh
./c6c/build/c6c -b -o prog prog.c6 && ./prog
# 越界时 stderr：c6c: 下标越界：index 5 不在 [0, 3) 之内，退出码 2
```

* 用**无符号**比较（`idx < n`）：负下标会变成巨大值，一次就抓住；
* 拦住之后直接退出（`write(2)` + `exit(2)`），不做恢复——越界写过的内存
  状态已经不可信，接着跑只会把损坏摊得更开；
* 检查跟着**数组类型**走：局部数组、多维数组的每一维、结构体里的数组字段
  都查（`m[2][0]`、`s.a[4]` 都会被拦）；
* 只查**编译期知道长度**的数组：指针下标（`int *p` 没有边界可言）和 VLA
  （长度在运行期才知道、类型里没有）不插检查，也就不会误报；
* 默认关闭：不加 `-b` 生成的目标文件里一条检查都没有（不改变默认行为，
  也不额外依赖运行时的 `c6c_bound_fail`）。

回归：`cmake --build build --target bcheck`（9 条：越界写/越界读各拦一次、消息点名下标与边界、
界内访问加不加 `-b` 输出一致、不加 `-b` 不检查、二维行越界、结构体数组字段
越界、指针下标与 VLA 不误报、`-run -b` 同效）。

## 库形态：把 c6c 嵌进别人的程序

`src/embed.c6` 把编译器包成一套 API（`include/c6lib.h`），同一份源码用
`-DC6C_NO_MAIN` 关掉 `main()` 就能编成库：

```sh
cmake --build build --target c6c-lib      # → c6c/build/libc6c.a + libc6c.so
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

**没有 `libtcc.h` / `tcc_*` 兼容层**：c6c 的宿主 API 只有 `c6lib.h` 里的
`c6_*` 一套；tcc 的嵌入头与库属于显式 `-DC6_ENABLE_TCC=ON` 的 c6 包。
原本写 `tcc_new` 那套的程序要改成 `c6_*`，不能靠换头/换库名顶替。

口径与限制：**一个进程只能编一次**（前端/后端/符号表都是文件级全局量，重置
那套还没做，换 state 也不行，见 `c6c/docs/todo.md`）；`c6_compile_file` 只收
`.c6`/`.c` 源文件，不收 `.o`/`.a`。

回归：`cmake --build build --target embedcheck`（5 条：动态库 / 静态库 /
落盘后独立跑 / 编译失败被报告 / 二次编译被拒绝）。

## 安装与包契约：没有 tcc 也能装出全套

`cmake -DC6_ENABLE_TCC=OFF` 时安装清单按 c6c 自己的搜索规则来定落点：

```
bin/c6                                  编译器（c6c 编的）
include/c6lib.h                         宿主 C 程序用的头（没有 libtcc.h）
lib/libc6c.a     lib/libc6c.so          嵌入库
lib/c6/rt.c6     lib/c6/include/*.h     编译器自带的运行时与头文件
lib/cmake/c6/c6Config.cmake             find_package(c6)
lib/pkgconfig/{c6,c6lib}.pc             pkgconf
```

c6c 用 `argv[0]` 推自己的目录去找 `<bin>/../lib/c6/{include,rt.c6}`，所以
整棵树搬走、换前缀都还能自己找到自带头文件。

无 tcc 的安装里只能用 c6c 自己的包：

```cmake
find_package(c6c 0.1 REQUIRED COMPONENTS compiler libc6c)
target_link_libraries(app PRIVATE c6c::libc6c)
```

需要 `find_package(c6 COMPONENTS libtcc)` 或 `<libtcc.h>` 的老消费者请用
`-DC6_ENABLE_TCC=ON` 构建的 c6 包；无 tcc 的包不提供 `c6::libtcc` 这个假目标。

```sh
cc $(pkgconf --cflags --libs c6lib) host.c -o host   # → -lc6c
```

注意 `include/` 里只放宿主用的 `c6lib.h`：c6 自己的 `stdio.h`/`stddef.h` 在
`lib/c6/include/`，装到系统 includedir 会把宿主程序的系统头文件盖掉。

回归：`cmake --build build --target pkgcheck`（11 条：无 tcc 也能 configure /
安装树齐全 / 没有 `libtcc.h` / `include/` 干净 / `find_package(c6 COMPONENTS libtcc)`
按预期失败 / `find_package(c6c)` 消费者真编真跑 / `pkgconf` 旗子
`-Wall -Werror` 过得去 / 搬到别的前缀的 `c6` 与 `c6c` 自找头文件与运行时）。

## 回归：让 `tests/regression.sh` 换编译器跑

根目录那 92 个 `std-*` 用例写在 `tests/regression.sh` 里，被测编译器现在是
**`$C6_CC`**（默认 `$C6_TCC_DIR/c6`，也就是 tcc 那一侧）。把它指到 c6c 就整批换
编译器：

```sh
C6_CC=c6c/build/c6c tests/regression.sh std-demo     # 单个用例
cmake --build build --target regression-c6c         # 已全绿的那批（列表见下）
./c6c/tests/regression-c6c.sh --all                 # 量基线：全部 std-* 用例
```

* `c6c/tests/regression-c6c.list` 是**单一数据源**：c6c 已经跑绿的用例名一行一个；
* `C6_ENABLE_TCC=OFF` 时 `cmake/C6Testing.cmake` 把这张表注册成 CTest 用例
  （label `regression-c6c`，同时也带 `c6c`），所以没有 tcc 的构建里也有真回归：
  `ctest --test-dir build-notcc -L regression-c6c -j 4`；
* selfhost / c99selfhost 那几个用例拿 tcc 的**源码**自举，只能由 tcc 跑，脚本里
  仍直接用 `$C6_TCC_DIR/c6`——它们不在迁移目标里。

当前基线（第 306 轮实测）：**92 / 92 全绿**，`regression-c6c.list` 92 条、CTest
97 个用例。之前挂到最后的 `std-https`（co 那两路）在第 306 轮修好：根因是
`__c6_co_run` 的主循环对 `blocked_on` 的处理——`await 某个 async 调用` 会在父任务里
建一个子任务，主循环要么无条件跳过被挡住的父任务（子任务结束之后没人叫醒它），
要么无条件恢复它（父任务空转，实测同一个协程被恢复到 guard 上限）。正确条件是
"只在等的那个任务还没结束时才跳过"，历史过程记在 `c6c/docs/todo.md` 第
301~306 轮。当前基线与剩余缺口以 [docs/acceptance.md](docs/acceptance.md) 和
根 `CHANGELOG.md` 为准；`todo.md` 是施工账本，不应当作现行缺口清单。

格式化那一片（`%f` 的舍入、`%g` 的尾零、`%.0d`、`%#x`、`-0.0`、1e19 以上的
整数部分、二进制转义 `"\xde"`）已经在第 212 轮对齐 glibc，并各钉了一个
check（`printfcheck` / `esccheck`）——这两条线不再是用例挂掉的原因。

顺带一个容易掉进去的坑：`rt.c6` 的 syscall 薄壳现在会**设 `errno`**（返回值
仍是 `-errno`）。在这之前 `errno == EINPROGRESS` / `EAGAIN` / `EINTR` 这类分支
永远走失败那条路——`std/net/tcp.c6` 的非阻塞 connect 就是这么被卡死的。

## 第二套 native 后端：aarch64

`--target=aarch64 -S` 让 c6c 自己生成 **aarch64 汇编**（`src/aout.c6`）——指令
选择、调用约定、栈帧都是 c6c 这一侧的事，汇编器只负责编码：

```sh
./c6c/build/c6c -S --target=aarch64 -nostdlib prog.c6 -o prog.s
clang --target=aarch64-linux-gnu -c prog.s -o prog.o
ld.lld -m aarch64elf -e _start -o prog.elf prog.o
qemu-aarch64 ./prog.elf; echo $?        # 与 x86-64 后端的退出码一致
```

和 x86-64 后端**同一套语义**（同一份 IR、同一套槽位模型）：

| 事项 | x86-64 | aarch64 |
| --- | --- | --- |
| 函数序言 | `push rbp; mov rbp,rsp; sub rsp,frame` | `stp x29,x30,[sp,#-16]!; mov x29,sp; sub sp,sp,frame` |
| vreg 槽位 | `[rbp-16*(n+1)]` | `x29 - 16*(n+1)`（偏移大时先算进 x17） |
| 二元运算 | a→rax、b→rcx | a→x0、b→x1 |
| 比较 | `cmp` + `setcc`（0/1） | `cmp` + `cset`（0/1） |
| 分支 | `test` + `jne` | `cbnz` |
| 调用 | rdi/rsi/…/r9 | x0..x7 |
| 返回 | `leave; ret` | `mov sp,x29; ldp x29,x30,[sp],#16; ret` |
| 退出 | `mov eax,60; syscall` | `mov x8,#93; svc #0` |

范围（v1）：整数/指针运算、控制流、调用（≤8 实参）、递归、结构体、全局数据、
`_start`。浮点、变参、VLA、内联汇编、TLS、>8 实参一律明确报错（
`c6c: aarch64 后端暂不支持 …`）并拒绝写文件。

回归：`cmake --build build --target a64check`（11 条：八个程序都是「c6c 出 aarch64 汇编 → clang
汇编 → ld.lld 链接 → qemu 真跑」，退出码与 x86-64 后端逐一相同；汇编里是
aarch64 指令、`_start` 走 `svc #0`；未实现的 op 拒绝写文件）。

## x86-64 之外：IR 与目标无关，经 LLVM 落到 aarch64

c6c 自己的 native 后端只出 x86-64 机器码；但 IR（`src/ir.c6`，24 字节定长记录）
是**与目标无关**的，`--emit-llvm` 出来的 `.ll` 交给任何 LLVM 目标后端都能落到
别的架构。这条链在这里被真跑验过（`cmake --build build --target archcheck`）：

```sh
./c6c/build/c6c --emit-llvm -nostdlib calc.c6 -o calc.ll
grep -v '^module asm' calc.ll > calc.pure.ll     # 运行时跳板是 x86-64 内联汇编
llc -march=aarch64 -filetype=obj -o calc.o calc.pure.ll
clang --target=aarch64-linux-gnu -c start.s -o start.o   # 自己写的 _start（svc #0）
ld.lld -m aarch64elf -e _start -o calc.elf start.o mem.o calc.o
qemu-aarch64 ./calc.elf; echo $?        # → 91，和 x86-64 后端一致
```

口径（别读错）：这不是"c6c 有 aarch64 native 后端"。目前的三个后端是
**native x86-64 / C99 / LLVM IR**；跨到 aarch64 走的是第三条路 + LLVM。运行时
（`lib/rt.c6`）里的 syscall 跳板是 x86-64 内联汇编，所以跨目标只覆盖"自己算、
不碰运行时"的程序；`llc` 会把零初始化变成 `memset` 调用，宿主侧得自己补。

回归：`cmake --build build --target archcheck`（4 条：x86-64 对照 91、IR→aarch64 汇编里是 aarch64
指令且没有 x86 寄存器、产物是 ARM aarch64 ELF、qemu 上跑出 91；缺
llc/clang/ld.lld/qemu-aarch64 时自动跳过）。
