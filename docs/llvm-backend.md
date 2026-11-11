# LLVM IR 后端

目标：复用整个前端（c6 的全部扩展），只换代码生成器——把前端的虚拟寄存器
机器翻译成文本 LLVM IR（`.ll`）。做法和 C99 输出后端完全一样：`tcc/llvm-gen.c`
是一份独立的单文件编译单元，编成 `tcc-llvm-p.o` 用 objcopy 把 `main` 改名成
`llvm_main`，再由 native 的 `main()` 在需要时转交。

## 怎么选到这个后端

**没有新增命令行选项。** 和 `-o out.c` 选 C99 后端一样，输出文件名以 `.ll`
结尾就选 LLVM 后端：

```
tcc/tcc -B tcc -o out.ll in.c6          # 生成 LLVM IR
clang -fuse-ld=lld -o out out.ll -L tcc -ltcc1
```

判据在 `tcc/tcc.c` 的 `wants_llvm_output()`：`-o` 给出的文件名以 `.ll` 结尾，
或者命令行给了 `--target=` / `--emit-llvm`（见下一节），并且没要求
`-E / -c / -r / -shared / -M* / -run`（这些仍然走 native 语义，`-c` 例外：
`--target` 会把 `-c` 变成“出目标平台的可重定位目标文件”）。
`-o <目录>` 仍然属于 C99 后端。

## 直接出目标平台的文件：`--target=`

`.ll` 之外还多了一条路：让它自己去调 clang 把 IR 编成目标文件，一条命令出结果。

```
tcc/tcc -B tcc --target=wasm32-unknown-unknown -o out.wasm in.c6
tcc/tcc -B tcc --target=x86_64-pc-linux-gnu   -o out in.c6 && ./out
```

| 选项 | 作用 |
| --- | --- |
| `--target=<triple>` | 选 LLVM 后端，模块头写这个 triple，`-o` 是最终产物；不给 `-o` 时是 `a.out`（`-c` 时 `a.o`） |
| `--emit-llvm` | 只要 `.ll`（等价于 `-o x.ll`，但可以配 `--target` 检查模块头） |
| `--cc=<compiler>` | 用哪个 C 编译器转 IR，默认 `$TCC_CC`，再默认 `clang` |

写了 `--target` 之后的流程是 `tcc_output_for_target()`：先把 IR 写到
`<outfile>.ll`，再用 `llvm_tool_cc()`（`tcc/llvm-link.c`）拼出

```
<cc> --target=<triple> -fuse-ld=lld -o <outfile> <outfile>.ll -L<tcc 目录> -ltcc1
```

目标运行库按 arch 分名：有 `tcc/libtcc1-<arch>.a` 就用它，否则用
`tcc/libtcc1.a`；两边都没有就报

```
tcc: no runtime for target 'wasm32-unknown-unknown': .../libtcc1-wasm32.a is missing
     build it with scripts/build-wasm-runtime.sh wasm32-unknown-unknown
```

（不会拿宿主的 `libtcc1.a` 硬凑：那是 x86-64 的目标文件，链接器只会报
`archive member ... is neither Wasm object file nor LLVM bitcode`。）

那份 `<outfile>.ll` 只是中间产物：成功就删掉，失败才留下并说一句
`tcc: LLVM IR left in 'out.wasm.ll'`——编译不过的时候正好用它复现。编译器的
退出码原样传出来（`fork` + `waitpid`，不是 `execvp` 顶掉自己）。要 IR 本身，
用 `--emit-llvm`；`-o x.ll` 配 `--target` 也只是写 IR，不往下走。

### 目标运行库

`libtcc1.c` 是普通 C，交叉编一遍就有了：

```
scripts/build-wasm-runtime.sh wasm32-unknown-unknown   # -> tcc/libtcc1-wasm32.a
make wasmrt WASM_TRIPLE=wasm32-unknown-unknown         # 同上
```

脚本用 clang `--target=<triple> -O1 -c` 编 `libtcc1.c stdatomic.c builtin.c
va_list.c dsohandle.c wasm-rt.c` 再 `llvm-ar rcs`。两处专门为目标平台让路：

- `libtcc1.c` 的 x87 `long double` 帮助函数（`__fixxfdi` / `__floatundixf` …）
  对 wasm 关掉，中间类型 `XFtype` 也退成 `double`：wasm 的 `long double` 是
  `f128`，任何运算都会引 compiler-rt 的 `__addtf3` / `__floatditf`，而 freestanding
  的 wasm 链接没有 compiler-rt。
- `tcc/lib/wasm-rt.c` 提供 libc/线程的最小桩（4 MiB 静态池上的
  `malloc/calloc/realloc/free`、`pthread_*`、`__cxa_atexit`、`select`），因为
  wasm 模块是 `-nostdlib` 链的。

### 预处理宏：只加不减

`--target` 只往预处理器里**加**目标的宏（`__wasm__` / `__wasm32__` /
`__aarch64__` / `__riscv` …），不会撤掉宿主的 `__x86_64__`。看着不对，但前端只有
一个类型模型（x86-64 的：`long` 和指针都是 64 位），而且读的还是**宿主的**
头文件：glibc 的 `gnu/stubs.h` 只要发现 `__x86_64__` 没了就去 include
`gnu/stubs-32.h`，机器上没装 32 位 glibc 头文件时连 `<stdio.h>` 都预处理不过
（`error: include file 'gnu/stubs-32.h' not found`）。所以判断“我在哪”要测
`__wasm32__` / `__aarch64__`，别测 `!__x86_64__`。

### wasm 没有 libc

wasm 目标用 `-nostdlib` 链，只带 `libtcc1`，所以除了运行库的帮助函数以外，
任何 import 都会在链接期变成 `undefined symbol`。模块里真引了外部函数时会先打
一条警告，省得只看到链接器的输出：

```
$ tcc/tcc -B tcc --target=wasm32-unknown-unknown -o out.wasm examples/gc_demo.c6
tcc: warning: wasm has no libc: printf, usleep imported, the link will fail
tcc: warning:   write freestanding code (no <stdio.h>), or provide those symbols yourself
wasm-ld-19: error: ...: undefined symbol: printf
```

`printf` 也不是“写个 C 版本就行”：后端输出的 IR 把所有实参都当 `i64` 传（LP64
模型），而 wasm32 上 libc 的 C 原型里指针和 `int` 是 32 位，签名对不上
（wasm-ld 会报 `signature mismatch`）。运行库自带的那几个帮助函数有专门的适配层
（见上），别的函数没有。**要跑在 wasm 上，就写 freestanding 的代码**：函数返回
值，由 JS 侧调 `exports`。GC 也是这样验证的——`make wasmtest` 里那个
`gc.c6` 用 `new` 分配、靠引用计数回收，外加切片和大结构体传参，全部在 node 里
跑出预期值。

### 目标 ABI 与 LP64 模型

这个后端输出的 IR 一直是 LP64 模型：`long`、`size_t`、指针在 IR 里都是 `i64`
（整个前端和 `%frmp` 偏移都按这个算）。`wasm64`/`aarch64`/`riscv64` 的 C ABI
本来就是这样，`wasm32` 不是（它是 ILP32），所以要一层适配：

- `build-wasm-runtime.sh` 编 `libtcc1.c` 时给 wasm32 加
  `-D__c6_gc_new=__c6_gc_new_lp64` 一类改名，把带 `unsigned long` / 指针参数的
  帮助函数编成 32 位版本；
- `wasm-rt.c` 再用 IR 期望的 64 位签名包一层。切片那对帮助函数不能这样转调
  （`libtcc1.c` 里 len 的位置是 `sizeof(void *)`，wasm32 上是 4，而 IR 里切片
  恒为 `{ ptr, i64 len }`，len 在偏移 8），所以在 `wasm-rt.c` 里按 IR 布局重写。
- 引用计数 GC 的头部字段是 `unsigned long long rc/meta`（不是 `unsigned long`）：
  `C6_GC_DEAD` 在 bit 63，`C6_GC_META_COUNT` 占低 40 位，32 位的 `long` 装不下。
- 内存 intrinsics 的长度参数跟随**目标的** index 类型：`llvm.memset.p0.i32` /
  `llvm.memcpy.p0.p0.i32`（wasm32），其他目标仍是 `.i64`。前端把 `memset` /
  `memcpy` / `memmove` 当普通 libcall 推上来，`gfunc_call()` 里
  `l_mem_intrinsic()` 把它们转成 intrinsics，这样 wasm 模块不必再要 libc 的
  `memset`（那里的 C 原型和 IR 的调用签名对不上：IR 是全 `i64`）。

### `wasm64` 为什么要等 LLD

`wasm64` 的 ABI 与 IR 的 LP64 模型天然一致，本来是最合适的目标，但 LLVM 19 的
`wasm-ld` 给函数指针表发出的 elem 段偏移是内存地址而不是表下标：

```
table:  flags=0x05（table64）min=4 max=4
elem:   i64.const 94545115086849, funcs=[27, 28, 107]
```

模块校验通过，一实例化就 `table index is out of bounds`。没有 elem 段的模块
（纯计算、不引 libtcc1）能正常跑，所以这是链接器的问题，不是 IR 的问题。
在此之前 wasm 冒烟跑 `wasm32`：

```
make wasmtest                                  # 默认 wasm32-unknown-unknown
./scripts/wasm-smoke.sh aarch64-unknown-linux-gnu   # 只出文件，没有 node 侧验证
```

`scripts/wasm-smoke.sh` 需要 clang（编运行库、转 IR）和 node（实例化），
缺 node 时只校验 `--emit-llvm` 的模块头。

想看看 GC 在 wasm 上怎么跑，最快的路径是

```
make wasmrt      # 编 tcc/libtcc1-wasm32.a
make wasmtest    # 出 .wasm 并用 node 跑：add/mul、GC 对、切片、大结构体
```

`examples/gc_demo.c6` 跑不了：它要 `printf` 和 `usleep`（见上）。

一次调用里的所有源文件合成**一个** `.ll`（与 `-o out.c` 把多个源文件合成一份
C 完全相同）：`-test` 的测试块编号在同一个状态里递增，跨翻译单元不会撞名。

## 结构

`tcc/llvm-gen.c` 和 `tcc/x86_64-gen.c` / `tcc/c99-gen.c` 是同一套接口
（`gfunc_prolog` / `gfunc_epilog` / `gfunc_call` / `gen_opi` / `gjmp` …），
所以前端的寄存器分配、`vstack` 约定、惰性求值顺序都不变。

生成的 IR 用一组固定的槽来承载前端的“寄存器”：

| 前端的值 | IR 里的位置 |
| --- | --- |
| `REG_IRET`/`REG_LRET`/`REG_R0..` | `%reg0`..`%reg7`，各 16 字节 alloca |
| 比较结果（`VT_CMP`） | `%cnd`，每个比较占 2 字节（含取反形式） |
| 局部变量（`VT_LOCAL`） | `getelementptr i8, ptr %frmp, i64 <负偏移>` |
| 较大的结构体返回 | 隐藏的 `sret` 指针参数 |

`%frmp` 指向帧顶：`%frame + @FRMT@`，帧大小 `@FRMA@`；两者在函数体被
`l_asm_body()` 拼装时替换。async 函数体没有自己的帧，`%frmp` 直接来自第二个
参数 `__c6_frm`，并在收尾时调用 `c6_frame_size_set()`。

函数体是**可嵌套**的：前端会在一个函数中途生成 `async` 块和函数字面量的体。
每次进 `gfunc_prolog` 把当前函数的指令表、标签表、补丁表、签名缓冲和帧大小
压栈，`gfunc_epilog` 再弹出恢复。

## 符号引用

地址在写文件时才最终确定（暂定声明会在原处分配，后面的定义可能搬家），所以
生成阶段只留下占位符，`llvm_output_file()` 里由 `l_subst()` 解析：

| 占位符 | 含义 |
| --- | --- |
| `@@N名字+偏移@@` | 有名字的符号（再按名字在符号表里查一次） |
| `@@K名字+偏移@@` | 同上，但是调用目标 |
| `@@O节号+偏移@@` | 匿名符号（字符串字面量、局部标签）：节内偏移永远不动 |
| `@@Q节号+偏移@@` | 同上，调用目标 |
| `@@A值@@` | 绝对地址 |
| `@@F名字@@` | libtcc1 里的帮助函数（没有 Elf 条目） |

占位符一律以 `@@` 开头：写好的函数体里已经含有 `@File_drop` 这类解析完的
引用，单 `@` 加一个字母的前缀会把 `@F` 当成帮助函数占位符，一路吃到下一个
`@` 为止（`errors/drop_bad` 当初就是这么坏的）。初始化列表那段文本不再走
`l_subst()`：它本来就是最终形态。

**不能存 Elf 符号下标**：前端在编译下一个输入文件时会重排、丢弃符号表条目，
生成阶段拿到的下标到写文件时可能越界或指向别的符号（早期版本就是这样把一段
IR 文本当成符号名输出过）。符号名本身也要做边界检查和合法性校验。

## 数据与重定位

每个已分配的数据节变成模块里的一个 `internal global [N x i8]` 字节数组：

- 有链接器名字的全局量额外发一个 alias：
  `@name = alias [size x i8], ptr getelementptr inbounds ([N x i8], ptr @__llvm_dS, i64 0, i64 偏移)`
- 数据节里的指针槽（vtable、指针全局量、函数字面量地址）在**载入时**由生成的
  `@__llvm_init()` 补齐，它挂在 `@llvm.global_ctors` 上
- `.init_array` / `.fini_array` 映射到 `@llvm.global_ctors` / `@llvm.global_dtors`
- c6 方法同时以 mangled 名和普通名可见：发 `@plain = weak alias i64, ptr @mangled`

指向静态函数的重定位记成“.text 节符号 + 偏移”，输出时用函数名还原
（`l_text_name()`）。

## `-test`

`-test` 模式下 `tcc` 直接跑 `test` 块。native 用 `runmain.o` + `tcc_run_tests()`，
这个后端写一个 IR 版的 `main`：依次调用 `__tcc_test_N()`，任一非 0 即失败，
再 `__c6_th_join_all()`，最后打印与 native/c99 完全相同的报告行

```
"%d test(s), %d failure(s)\n"
```

所以 `tests/run-tests.sh` 能用同一套断言检查三个后端。`-test` 里用户自己的
`main` 是死代码（native 也不会调用它），生成阶段直接丢掉，避免和 runner 撞名。

## 跑测试

```
make build
BACKEND=llvm LLVMCC=/usr/lib/llvm-19/bin/clang ./tests/run-tests.sh
```

`LLVMCC` 默认 `clang`；找不到时会自动试 `/usr/lib/llvm-19/bin/clang`。
`LLVMFLAGS` 默认 `-fuse-ld=lld`——`tcc/libtcc1.a` 的 `.eh_frame` 会让 GNU ld
报 `overlapping FDEs`，lld 没有这个问题。

## 现状

`tests/` 的 101 组用例里 96 组通过（native 与 C99 后端全绿）。仍未通过：

- `closure` / `fn_lit` / `abi/fn_lit`：闭包（捕获环境）与函数字面量作为值传递
  时，间接调用的目标地址不对，运行期崩。`tests/closure.c6` 里能缩到“闭包捕获
  一个切片”：帧里有一个槽（`%frmp-104`）只被读、从没被写，于是解引用了垃圾。
- `errors/closure_capture_type`：正向路径同样受上面的闭包问题影响。
- `abi/sret12`：9..16 字节的 INTEGER 类结构体返回应该走 `rax:rdx` 两个寄存器，
  这个后端目前一律走隐藏 `sret` 指针，与另外两个后端的 ABI 不一致（改到一半的
  两寄存器返回实现会连带弄坏 `slice`/`tuple` 等跨翻译单元用例，先搁置）。

链接期诊断：`.ll` 只是一个编译单元，谁该由 libc 提供这里无从判断，所以
`errors/package` 那种 `unresolved reference` 由外部链接器报。用例里用
`// EXPECT-LLVM 片段: 文本` 声明本后端自己的措辞。

## 文件

| 文件 | 作用 |
| --- | --- |
| `tcc/llvm-gen.c` | 代码生成 + 模块拼装 |
| `tcc/llvm-link.c` | `llvm_tool_cc()`：拼 clang 命令行、找目标运行库 |
| `tcc/tcc.c` | `wants_llvm_output()` 与 `llvm_main` 转交 |
| `tcc/tccelf.c` | `tcc_output_file()` 分发到 `llvm_output_file()` |
| `tcc/libtcc.c` | `TCC_OUTPUT_LLVM`、`-c`/`-test` 的输出类型 |
| `tcc/lib/wasm-rt.c` | 目标运行库的 libc/线程桩 + ILP32 适配（只在 `__wasm__` 编） |
| `scripts/build-wasm-runtime.sh` | 交叉编 `tcc/libtcc1-<arch>.a` |
| `scripts/wasm-smoke.sh` | `--target` 的端到端冒烟（`make wasmtest`） |
| `tests/run-tests.sh` | `BACKEND=llvm` 分支 |
