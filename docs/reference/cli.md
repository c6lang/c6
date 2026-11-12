# 命令行与构建目标

> **状态**：现行参考 · **索引**：[文档索引](../README.md) · **测试细节**：[manual/testing.md](../manual/testing.md) · **C99 后端**：[c99-backend.md](c99-backend.md)
>
> 一句话：编译器 `tcc/c6` 的选项速查 + `Makefile` 全部目标。**语言开关不是选项**，
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

## make 目标

`make help` 打印同一份清单。分组按「你此刻想干什么」。

### 构建

| 目标 | 作用 |
|---|---|
| `make bootstrap` | 首次配置 `tcc/`（已有 `config.mak` 就跳过，不联网） |
| `make build` | 扩展模式构建（`EXTRA-DEFS=-DC6_EXT`），产物 `tcc/c6` |
| `make build-plain` | 上游模式构建（不定义 `C6_EXT`），用来验证「扩展关闭零行为变化」 |
| `make check-tcc` | 检查 `tcc/` 源码是否完整 |
| `make mode-ext` / `mode-plain` | 切换构建模式（会自动清理旧产物） |
| `make clean` / `distclean` | 清理构建产物 / 连 `configure` 结果一起清理（不删源码） |

### 测试与回归

| 目标 | 作用 |
|---|---|
| `make test` | 上游 `tests2` 全量（扩展模式） |
| `make test-plain` | 关闭扩展重建后跑同一套上游测试 |
| `make selftest` | `tests/` 下全部 c6 自测（test 块 + 编译期诊断） |
| `make errcheck` | `errbase.def` 本身与 C126–C128 的检查 |
| `make test-all` | `selftest` + `test` + `c6selfcheck` + 标准库 + freestanding/裸机 |
| `make check-all` | **提交/发布前跑这一条**：`test-all` + `errcheck` + C99 后端两套 + 三条自举路线 |

### 标准库

| 目标 | 作用 |
|---|---|
| `make stdtest` | 遍历 `std/` 下七个子目录，跑每个模块的 `test` 块 |
| `make std-demo` | 编译并运行 `examples/wordcount.c6`（标准库集成示例） |
| `make std-tour` | 编译并运行 `examples/stdtour.c6`（新模块自带自检） |
| `make std-kit` | 编译并运行 `examples/stdkit.c6`（第三批模块的拼装示例） |
| `make std-box` | 编译并运行 `examples/stdbox.c6`（第四批模块的拼装示例） |
| `make std-web` | 编译并运行 `examples/stdweb.c6`（第五批模块的拼装示例） |
| `make std-net` | 编译并运行 `examples/stdnet.c6`（第六批模块的拼装示例） |
| `make std-more` | 编译并运行 `examples/stdmore.c6`（第七批模块的拼装示例） |
| `make std-next` | 编译并运行 `examples/stdnext.c6`（第八批模块的拼装示例） |
| `make std-node` | 编译并运行 `examples/stdnode.c6`（第九批模块的拼装示例） |

### freestanding、裸机与交叉目标

| 目标 | 作用 |
|---|---|
| `make fstest` | 无 libc 冒烟：不链接 crt/libc，入口是 `_start` |
| `make fstest-go` | freestanding + 程序自己实现的 pthread 层（`spawn` / `thread`） |
| `make fstest-noos` | 完全没有操作系统：程序里一个 syscall 都没有 |
| `make fstest-qemu` | 用 `qemu-x86_64` 用户态模拟器跑（需要 qemu-user） |
| `make fstest-bare` | 512 字节 BIOS 引导扇区，在 `qemu-system-x86_64` 空机器上跑 |
| `make fstest-bare-tests` | 把 `tests/*.c6` 的 test 块搬上裸机跑 |
| `make fstest-bare-go` | 裸机上的 `go`：自己写的抢占式调度器（8259A + 8254） |
| `make fstest-bare-handle` | 裸机上的 `spawn` 句柄：`h->join()` / `h->value()` |
| `make fstest-bare-smp` | 裸机 SMP：2 核与 1 核各跑一遍 |
| `make wasmrt` | 编目标平台运行库 `tcc/libtcc1-<arch>.a`（需要 clang） |
| `make wasmtest` | `--target=<triple>` 出 `.wasm`，再用 node 跑一遍 |

### C99 输出后端

| 目标 | 作用 |
|---|---|
| `make c99` | 兼容旧脚本的占位目标（C99 后端已内置，用 `-c99`） |
| `make c99test` | 与 `selftest` 同一套用例，经 C 后端后用 `gcc` 编译运行 |
| `make c99tcc-test` | 同上，宿主编译器换成 `tcc/c6`（整条链不需要 gcc） |

### 自举

| 目标 | 作用 |
|---|---|
| `make c6selfcheck` | `tcc/tcc.c` 拷成 `.c6` 用 c6 规则编译（「变量必须初始化」最严格的回归） |
| `make selfhost` | c6 路线：`tcc` → `stage1` → `stage2`，两次目标文件逐字节比对 |
| `make c99selfhost` | C99 路线：生成 C → `gcc` → `stage2` → `stage3` → 比对 |
| `make c99selfhost-tcc` | 同上，但生成的 C 也由 tcc 自己编（不需要 gcc） |

三条自举路线都会做 bootstrap fixpoint 比对，再用最终编译器编并运行一个小程序；
产物在 `build/selfhost/`（`make clean` 一起删）。

## 环境变量

| 变量 | 默认 | 作用 |
|---|---|---|
| `JOBS` | `nproc` | 并行构建的作业数 |
| `CFLAGS` | `-g -O0 -Wall -Wno-unused-result` | 构建 tcc 的 CFLAGS |
| `HOSTCC` | `gcc` | 宿主 C 编译器 |
| `C99CC` | `gcc` | `make c99test` 用的宿主编译器（`C99CC=clang` 也行；`make c99tcc-test` 用 `tcc/c6`） |
| `C99FLAGS` | gcc 的一整套严格开关 | 编译生成 C 的开关（换成 tcc 时自动去掉 gcc 专有开关） |
| `LLVMCC` | `clang` | `wasmrt` / `wasmtest` / 交叉目标用的 LLVM 编译器 |
| `WASM_TRIPLE` | `wasm32-unknown-unknown` | 交叉目标三元组 |
