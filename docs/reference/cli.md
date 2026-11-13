# 命令行与构建目标

> **状态**：现行参考 · **索引**：[文档索引](../README.md) · **测试细节**：[manual/testing.md](../manual/testing.md) · **C99 后端**：[c99-backend.md](c99-backend.md)
>
> 一句话：编译器 `tcc/c6` 的选项速查 + `tests/regression.sh` 全部回归用例。**语言开关不是选项**，
> 是源文件后缀 `.c6`；`-std=c6` 已删除，传了会直接报错。

## 编译器选项

### c6 专用（只在 `C6_EXT` 构建里存在）

| 选项 | 作用 |
|---|---|
| `-test` | 跑源文件里的 `test "描述" { ... }` 块；没有 `main` 也能跑，有 `main` 时被测试运行器替换 |
| `-test-filter=PATTERN` | 只跑名字匹配的 test 块（配合 `-test`） |
| `-emit-header FILE` | 配合 `-c`，把 `module` 的导出写成普通 C 头文件（切片/元组生成 `NAME__slice_N` / `NAME__tuple_N` typedef） |
| `-emit-errbase[=FILE]` | 把这次编译能看见的所有模块的错误码号段一次钉住；不带 `=FILE` 写 `./errbase.def` |
| `-errbase-file=FILE` | 指定别的钉号表（必须存在，否则报 C128） |
| `-no-errbase` | 完全不读钉号表（自动号段照旧） |
| `-Wgeneric-depth` / `-Wno-generic-depth` | 泛型实例嵌套深度告警（默认开） |
| `--ub FILE|DIR...`（别名 `-fub`） | 不编译，直接把源码交给 UB 检查器 `stdub`（第八十四批）；给目录会递归扫里面所有 `*.c6`：`--ub -W` 走安全门模式、`--ub -S` 走健全模式；找不到 `stdub` 时用 `C6_STDUB=/path` 指定。详见 [ubcheck.md](../ubcheck.md) |

### 后端选择

| 选项 | 作用 |
|---|---|
| `-c99` | 选用 C99 输出后端（生成 C 源码，不是机器码） |
| `-o *.c` / `-o <目录>` / `-o <路径>/` | 不写 `-c99` 也会自动选中 C 后端；`-o <目录>` 给每个输入 TU 一个 `<目录>/<输入>.c` |
| `-emit-llvm` | 输出 LLVM IR（见 [llvm-backend.md](llvm-backend.md)） |
| `--target=TRIPLE`（或 `-target=`） | 交叉目标；IR 交给 `clang`，见 [llvm-backend.md](llvm-backend.md) |
| `-cc <编译器>` | 交给哪个宿主编译器处理 IR |

### 常用 tcc 选项（本仓库每天都在用）

| 选项 | 作用 |
|---|---|
| `-B tcc` | 让未安装的编译器找到自己的 `libtcc1.a` 与运行时头文件（几乎每条命令都要带） |
| `-run file.c6 [...]` | 编译并立刻运行，不落盘 |
| `-c` / `-o FILE` | 只编译 / 指定输出 |
| `-I DIR` / `-D` / `-U` | include 路径与宏 |
| `-E` | 只预处理 |
| `-g` / `-O0..3` | 调试信息 / 优化 |
| `-b` | 边界检查（不能与 `async` 同用，C67） |
| `-shared` / `-r` / `-nostdlib` / `-pthread` / `-Wl,...` | 链接动作与库 |
| `-std=c99` 等 | 只影响被编译的 C 语言；`-std=c6` 是错误（后缀才是开关） |

典型命令：

```sh
./tcc/c6 -B tcc -run examples/err_demo.c6                 # 直接跑
./tcc/c6 -B tcc -test tests/defer.c6                      # 跑 test 块
./tcc/c6 -B tcc -c -emit-header geo/shape.h geo/shape.c6  # 导出 C 头文件
./tcc/c6 -B tcc -o out.c prog.c6 && cc -std=c99 -o prog out.c   # C99 后端
```

## CMake 构建目标与 CTest 用例

`cmake --build build --target c6-help` 打印常用命令；`--target help` 列出全部
CMake target。测试由 CTest 统一调度：每个回归用例注册为同名 CTest 测试，用
`ctest --test-dir build -R '^<用例名>$' --output-on-failure` 跑单个，用
`-L daily` / `-L regression -LE aux -j N` / `-L std -j 1` 跑聚合。标签、超时、
并发互斥、日志与失败重跑都由 CTest 负责；同名 CMake target 仍保留，但只是
转发给 `ctest -R` 的兼容薄壳，语义与直接跑 CTest 完全一致（`BUILD_TESTING=OFF`
时同名目标改为打印提示）。**只有回归用例能换成 `ctest -R`**：`build-plain`、
`test-plain`、`c6-clean` 这类构建/清理动作没有对应的 CTest 用例，`test` 也是
CMake 内置目标。

分组按「你此刻想干什么」。

### 构建

| 目标 | 作用 |
|---|---|
| `cmake -S . -B build` | 在构建目录生成 `config.mak` / `config.h` / `config.texi`（配置期对源码树零写入；构建期 `tcc-config` 每次比对并按需同步、清理旧目标文件，不联网） |
| `cmake --build build` | 扩展模式构建（`EXTRA-DEFS=-DC6_EXT`），产物 `tcc/c6` |
| `cmake --build build --target build-plain` | 上游模式构建（不定义 `C6_EXT`），用来验证「扩展关闭零行为变化」 |
| `cmake --build build --target check-tcc` | 检查 `tcc/` 源码是否完整 |
| `cmake --build build --target c6-clean` | 清理构建产物与自举产物 |
| `cmake --build build --target clean-all` | 连生成的 `config.mak` / `config.h` 一起清理（不删源码，之后要重新 `cmake -S . -B build`） |

模式由 `-DC6_ENABLE_EXT=ON/OFF` 决定；`mode-ext` / `mode-plain` 这两个同名目标现在
只打印重新配置的提示，不再往 `tcc/` 写 `.c6-mode` 并清理产物。

c6 自己的文档由 `C6_INSTALL_DOCS`（默认 `ON`）装到 `C6_INSTALL_C6_DOCDIR`
（默认 `<datarootdir>/doc/c6`，即 `/usr/share/doc/c6`）：`docs/` 的 33 篇文档
保留子目录结构放在 `docs/` 下，`README.md` / `CHANGELOG.md` / `copyright` 放在
它的上一层，因此 `docs/README.md` 里 `../README.md` 之类的相对链接仍然可用。
上游 tcc 的 `tcc-doc.html` 仍装在 `C6_INSTALL_DOCDIR`（默认
`<datarootdir>/doc/tcc`）。文档里指向 `examples/` / `tests/` 源码的链接只在
仓库内有效，安装包里不携带源码。

`tcc/` 里的目标文件全部依赖一份标志记录 `tcc/.c6-flags`（内容是本轮编译用的
`CC` / `CFLAGS` / `DEFINES` / `LDFLAGS` / `LIBS`）。标志一变（切换 `build` /
`build-plain`、换一份 `CFLAGS`），下一次 `cmake --build build` 就把所有目标文件
重编一遍。这不是洁癖：`-DC6_EXT` 会改 `struct TCCState` 在 `tcc.h` 里的布局，
一半对象带它、一半不带，链接照样通过，但驱动读到的 `files` / `nb_files` 是错位的
槽位 —— 典型症状是任何输入都报 `tcc: error: no input files`。所以别手工删一部分
产物来「加快」构建，`cmake --build build --target c6-clean` 才是正确的清理方式。

此外 `tcc/CMakeLists.txt` 在构建目录生成 `tcc/generated/config.sig`，记录
`EXTRA-DEFS` / `CFLAGS` / 额外 make 变量与 `CONFIG_*`：`tcc-config` 每次构建
拿它和 `config.applied` 比较，这些只走 make 命令行、不写进 `config.mak` 的
参数一变也会清掉旧目标文件。`config.mak` 里的 `# c6-build-dir:` 记录所属
构建目录；检测到别的构建目录留下的配置时，配置阶段给出警告、构建阶段比对
后自动同步回来（产物落在源码树，不能并行共用）。

### 测试与回归

| 目标 | 作用 |
|---|---|
| `ctest --test-dir build -R c6-tests2` | 上游 `tests2` 全量（扩展模式） |
| `cmake --build build --target test-plain` | 关闭扩展重建后跑同一套上游测试 |
| `cmake --build build --target selftest` | `tests/` 下全部 c6 自测（test 块 + 编译期诊断） |
| `cmake --build build --target errcheck` | `errbase.def` 本身与 C126–C128 的检查 |
| `cmake --build build --target test-all` | `selftest` + `test` + `c6selfcheck` + 标准库 + freestanding/裸机（等价于 `ctest --test-dir build -L core -j N`） |
| `cmake --build build --target check-all` | **提交/发布前跑这一条**：全部回归门禁用例（等价于 `ctest --test-dir build -L regression -LE aux -j N`）；每用例独占 `build/tests/<用例>/`，整组并发，fstest / tests2 / source-tree 三组用 `RESOURCE_LOCK` 互斥。`-DC6_CHECK_JOBS=1` 则整轮串行 |
| `cmake --build build --target check-aux` | 单独跑 4 个 `aux` 用例（`check-tcc` / `ubcheck` / `safegate` / `bench-async`）；`bench-async` 需要 g++/rustc/go |

### 标准库

| 目标 | 作用 |
|---|---|
| `cmake --build build --target stdtest` | 遍历 `std/` 下七个子目录，跑每个模块的 `test` 块 |
| `cmake --build build --target std-demo` | 编译并运行 `examples/wordcount.c6`（标准库集成示例） |
| `cmake --build build --target std-tour` | 编译并运行 `examples/stdtour.c6`（新模块自带自检） |
| `cmake --build build --target std-kit` | 编译并运行 `examples/stdkit.c6`（第三批模块的拼装示例） |
| `cmake --build build --target std-box` | 编译并运行 `examples/stdbox.c6`（第四批模块的拼装示例） |
| `cmake --build build --target std-web` | 编译并运行 `examples/stdweb.c6`（第五批模块的拼装示例） |
| `cmake --build build --target std-net` | 编译并运行 `examples/stdnet.c6`（第六批模块的拼装示例） |
| `cmake --build build --target std-more` | 编译并运行 `examples/stdmore.c6`（第七批模块的拼装示例） |
| `cmake --build build --target std-next` | 编译并运行 `examples/stdnext.c6`（第八批模块的拼装示例） |
| `cmake --build build --target std-node` | 编译并运行 `examples/stdnode.c6`（第九批模块的拼装示例） |
| `cmake --build build --target std-form` | 编译并运行 `examples/stdform.c6`（第十批模块的拼装示例） |
| `cmake --build build --target std-embed` | 编译并运行 `examples/stdembed.c6`（第十一批：crc/fixed/pool/frame 嵌入式基础） |

### freestanding、裸机与交叉目标

| 目标 | 作用 |
|---|---|
| `cmake --build build --target fstest` | 无 libc 冒烟：不链接 crt/libc，入口是 `_start` |
| `cmake --build build --target fstest-go` | freestanding + 程序自己实现的 pthread 层（`spawn` / `thread`） |
| `cmake --build build --target fstest-noos` | 完全没有操作系统：程序里一个 syscall 都没有 |
| `cmake --build build --target fstest-qemu` | 用 `qemu-x86_64` 用户态模拟器跑（需要 qemu-user） |
| `cmake --build build --target fstest-bare` | 512 字节 BIOS 引导扇区，在 `qemu-system-x86_64` 空机器上跑 |
| `cmake --build build --target fstest-bare-tests` | 把 `tests/*.c6` 的 test 块搬上裸机跑 |
| `cmake --build build --target fstest-bare-go` | 裸机上的 `go`：自己写的抢占式调度器（8259A + 8254） |
| `cmake --build build --target fstest-bare-handle` | 裸机上的 `spawn` 句柄：`h->join()` / `h->value()` |
| `cmake --build build --target fstest-bare-smp` | 裸机 SMP：2 核与 1 核各跑一遍 |
| `cmake --build build --target wasmrt` | 编目标平台运行库 `tcc/libtcc1-<arch>.a`（需要 clang） |
| `cmake --build build --target wasmtest` | `--target=<triple>` 出 `.wasm`，再用 node 跑一遍 |

### C99 输出后端

| 目标 | 作用 |
|---|---|
| `cmake --build build --target c99` | 兼容旧脚本的占位目标（C99 后端已内置，用 `-c99`） |
| `cmake --build build --target c99test` | 与 `selftest` 同一套用例，经 C 后端后用 `gcc` 编译运行 |
| `cmake --build build --target c99tcc-test` | 同上，宿主编译器换成 `tcc/c6`（整条链不需要 gcc） |

### 自举

| 目标 | 作用 |
|---|---|
| `cmake --build build --target c6selfcheck` | `tcc/tcc.c` 拷成 `.c6` 用 c6 规则编译（「变量必须初始化」最严格的回归） |
| `cmake --build build --target selfhost` | c6 路线：`tcc` → `stage1` → `stage2`，两次目标文件逐字节比对 |
| `cmake --build build --target c99selfhost` | C99 路线：生成 C → `gcc` → `stage2` → `stage3` → 比对 |
| `cmake --build build --target c99selfhost-tcc` | 同上，但生成的 C 也由 tcc 自己编（不需要 gcc） |

三条自举路线都会做 bootstrap fixpoint 比对，再用最终编译器编并运行一个小程序；
产物在各自的 `build/tests/<用例>/selfhost/`（`cmake --build build --target c6-clean`
一起删）。

## 环境变量

构建期的并行度、CFLAGS、宿主编译器都是 `cmake -S . -B build -D...` 的 cache
参数，完整清单见 README 的参数表；常用的几个：

| CMake 参数 | 默认 | 作用 |
|---|---|---|
| `C6_TCC_JOBS` | CPU 核数 | 非 Makefile 生成器下子 make 的 `-j` |
| `C6_TCC_CFLAGS` | `-g -O0 -Wall -Wno-unused-result` | 构建 tcc 的 `CFLAGS` |
| `C6_HOSTCC` | `gcc` | 自举 / `c6selfcheck` 的宿主编译器 |
| `C6_CHECK_JOBS` | 空（= `C6_TCC_JOBS`） | `check-all` / `test-all` 的 CTest `-j` |
| `C6_LLVMCC` / `C6_LLVMAR` / `C6_NODE` / `C6_WASM_TRIPLE` | 空 | wasm 目标的工具链与 triple |

运行时变量由 `cmake/C6Testing.cmake` 通过 CTest 的 `ENVIRONMENT` 注入；脚本里
都有同名默认值，单独运行 `tests/regression.sh <用例>` 也能工作：

| 变量 | 默认 | 作用 |
|---|---|---|
| `C6_CASE_OUT` | 单独运行时 `build/` | 每用例独立输出目录（`build/tests/<用例>/`） |
| `C6_TCC_DIR` / `C6_MAKE` | `tcc` / `make` | 编译器目录与 make 可执行文件 |
| `C6_SELFHOST` / `C6_BARE` / `C6_TMP` | `<C6_CASE_OUT>/{selfhost,bare,tmp}` | 自举、裸机与临时产物目录 |
| `TCC` / `C99TCC` | `tcc/c6` | `tests/run-tests.sh` 用的编译器 |
| `BACKEND` | `native` | `native` / `c99` / `llvm` |
| `C99CC` | `gcc` | C99 后端的宿主编译器（`clang` 也行） |
| `C99FLAGS` | gcc 的一整套严格开关 | 编译生成 C 的开关（换成 tcc 时自动去掉 gcc 专有开关） |
| `LLVMCC` / `LLVMFLAGS` | `clang` / `-fuse-ld=lld` | LLVM 后端的编译器与链接开关 |
