# C99 输出后端（`-c99`、`-o *.c`、`-o <dir>`）

> **状态**：现行实现 · **索引**：[文档索引](../README.md) · **同级后端**：[llvm-backend.md](llvm-backend.md)
>
> 一句话：复用完整前端，把机器码换成**可移植的 C99 源码**；后端编译进同一个 `tcc/c6`，没有第二个编译器。

`tcc/c99-gen.c` 是第二个代码生成后端：它复用完整前端，把**可移植的 C99 源码**写出来，
而不是机器码。它编译进普通的 `tcc/c6` 二进制里，没有单独的编译器。

```sh
cmake --build build                                  # 一个 tcc，两个后端
./tcc/c6 -B tcc -o out.c prog.c              # prog.c -> out.c（.c 后缀选中 C 后端）
./tcc/c6 -B tcc -c99 -o out.c prog.c         # 同上，显式指定
cc -std=c99 -o prog out.c && ./prog         # 用任意 C 编译器编结果

./tcc/c6 -B tcc -c -o out/ a.c b.c          # out/a.c、out/b.c（每个 TU 一个 .c）

cmake --build build --target c99test                                # 与 selftest 相同的用例，走 C 后端
cmake --build build --target c99tcc-test                            # 同上，但宿主编译器换成 tcc 自己
```

`cmake --build build --target c99test` 跑与 `cmake --build build --target selftest` 相同的语料：`tests/*.c6`、`tests/c6ext/*.c6`、
`tests/errors/*.c6`、`tests/exit/*.c6`、`tests/abi/*.c6`、`tests/c99/*.c6` 每个用例都先
经同一个 `tcc/c6`（用 `-o ...c` 要 C 源码），再经
`gcc -std=c99 -pedantic -Wall -Wextra -Werror -pthread` 编译，退出码、输出和诊断断言都相同。
仅因 C 输出后端而不同的诊断，在 `tests/errors/*.c6` 里用 `// EXPECT-C99` 单独声明，
其余断言共享。`C99CC=clang cmake --build build --target c99test` 换宿主编译器，`BACKEND=c99 tests/run-tests.sh`
直接跑同一套。`cmake --build build --target c99tcc-test` 用 `C99CC=tcc/c6` 跑同一套语料（`C99FLAGS` 去掉
gcc 专有开关），所以生成的 C 也必须满足 tcc——代价见下面「用 C 后端自举」。
`tests/c99/c99test.c6` 另外用一个程序同时覆盖普通 C 与下面列出的所有 c6 特性。

`-c99` 只选输出后端，不是语言标准开关，被编译的语言仍然是 C。原生二进制里两个生成器都在：
`-c99` 选中 C 生成器；`-o` 指向 `.c` 文件、已存在的目录或以 `/` 结尾的路径时也会自动选中。
所以 `./tcc/c6 -B tcc -o out.c prog.c` 写出的是 C 源码；只给 `-c99` 不给 `-o` 时默认写
`a.c`。`-c` 表示「每个翻译单元一个生成的 C 文件」，`-test` 配合 `.c` 输出会写出生成的
测试运行器文件。`-o <dir>` 让每个输入 TU 得到自己的 `<dir>/<input>.c`，所以
`-c -o out/ a.c b.c` 不经 `-c99` 也产出 `out/a.c` 与 `out/b.c`。预处理（`-E`）和链接动作
（`-shared`、`-r`、`-run`）保持原生语义；向 C 后端要机器码/链接动作会报错而不是产出空文件；
会覆盖自己输入的输出文件被拒绝。`-B tcc` 是必需的，工具要靠它找到 tcc 自己的 `include/`。
多架构系统 include 目录从 `config.h` 的 `CONFIG_TRIPLET` 加上；宿主三元组没被识别时，
手工加 `-I/usr/include/$(cc -print-multiarch)`。`cmake --build build` 需要 `objcopy`（binutils；
`llvm-objcopy` 也行）来本地化内嵌编译器的符号。

生成的文件只需要 `<string.h>` 的 `memcpy`/`memmove`/`memset`：vstack 寄存器变成
`union` 数组 `__c99_r[8]`，栈帧是按 tcc 自己的局部偏移寻址的字节数组 `__c99_frm.f`，
比较结果物化进 `__c99_cnd`，数据段以字节数组发出，启动时由 `__c99_init()` 按重定位表打补丁。

普通 C 程序可用：整数、浮点、指针、数组与字符串字面量；局部与全局；`if`/`while`/`for`/
`do`/`switch`；递归与直接/间接调用；`static` 函数；`?:`、位运算、移位与全部类型转换；
结构体、联合体与返回结构体的函数；变参函数定义（`va_start`/`va_arg`/`va_end`，以及把
`va_list` 传给 `vsnprintf` 这类宿主函数），采用 x86-64 SysV 的 `va_list` 布局。

只被这个 TU 导入的符号（libc 调用、别的 TU 的函数或对象）统一声明一次，用私有的
`__c99_ext_N` 名字加 `__asm__` 标签绑到真正的链接符号；生成的代码通过源名的 `#define`
够到它们，这层 `#define` 在运行时前导代码之前又会撤掉。这样声明块可以保留编译器自己的
原型观（tcc 的 `long` 与 `long long` 是同一个 64 位整数类型，它看到的每个 struct 都是
不透明字节块），又不会与前导代码 `#include <stdlib.h>`、`<pthread.h>` 时的宿主声明冲突。
`cc out.c`（不加 `-w`、不加额外 include 开关）就够。

c6 程序同样可用。一个 c6 翻译单元（`.c6` 源文件）的产物末尾会附上一份可移植的 C 实现，
覆盖 defer/错误/切片/协程/线程运行时。它就是普通 C99 加 POSIX（`async`/`await`、`spawn`
和 `c6_io` 用到 `pthread` 与 `select`），不需要 `libtcc1`。运行库符号在 GCC/Clang 上是
weak 链接，所以同一个程序的所有生成 TU 共享一份运行时（错误槽、协程与 join 都能跨 TU）；
没有 weak 符号的编译器退化为每 TU 一份私有副本。`tests/*.c6` 覆盖的特性包括
`defer`/`errdefer`、`err`、`try`/`catch`、切片、`foreach`、元组、方法/接口、`typename`、
`async`/`await`、`co`、`spawn`、字符串插值（`f".."` / `@f".."`；运行时前导代码带着同一组
`__c6_fmt_*` 构造器）和 `test` 块：

```sh
./tcc/c6 -B tcc -o out.c prog.c6
cc -std=c99 -pedantic -Wall -Wextra -Werror -o prog out.c
./prog
```

带 `test` 块但没有 `main` 的生成文件会得到一个合成的 `main` 来跑它们；有重定位但没有
`main` 的文件改得到一个 `__attribute__((constructor))` 初始化器。带 `-test` 时合成的运行器
替换用户 `main`（改名成 `__c99_orig_main`），与原生 `-test` 运行器一致；`cmake --build build --target c99test`
就是这样驱动 test 块文件的。

不超过 8 字节、只含整数/指针字段的结构体走 x86-64 寄存器返回 ABI，所以能与普通 C 互操作
（`tests/abi/`）。9–16 字节且字段全是整数/指针的结构体同样可以：生成的 C 按值返回它们，
宿主编译器就把它们放进 `rax:rdx`，与原生后端一致（`tests/abi/sret12.c6`）。带浮点字段的
结构体和大于 16 字节的结构体用这个后端自己选的隐藏结果指针：c6 TU 之间一致，大于 16 字节
时也与原生 ABI 一致，但把含 `float`/`double` 的 9–16 字节结构体与普通 C 混用不受支持。

不支持（都给出明确错误而不是错编译）：计算 goto 与变长数组。非 POSIX 宿主上生成的运行时
仍能编译，但 `async`/`spawn`/`c6_io` 需要平台移植。

## 用 C 后端自举

`cmake --build build --target c99selfhost` 是端到端证明：生成的 C 是一个能用的编译器，不只是能用的代码。

```sh
./tcc/c6 -B tcc -DONE_SOURCE=1 -DC6_EXT -c99 -o build/selfhost/c99/tcc-c99.c tcc/tcc.c
cc -std=c99 -o build/selfhost/c99/tcc-stage2 build/selfhost/c99/tcc-c99.c -lm -ldl -lpthread
build/selfhost/c99/tcc-stage2 -B tcc -DONE_SOURCE=1 -DC6_EXT -c tcc/tcc.c -o stage3.o
build/selfhost/c99/tcc-stage2 -B tcc -o tcc-stage3 stage3.o -lm -ldl -lpthread
build/selfhost/c99/tcc-stage3 -B tcc -DONE_SOURCE=1 -DC6_EXT -c tcc/tcc.c -o stage4.o
cmp stage3.o stage4.o                    # 自举不动点
```

`stage2` 是宿主 C 编译器从 tcc 的 C 输出编出来的完整编译器（自己的 x86-64 后端、链接器和
驱动）；`stage3` 由 `stage2` 从原始源码编出，两者产出的目标文件逐字节相同。`cmake --build build --target selfhost`
做同一条往返但不绕 C（`tcc` → `stage1` → `stage2`，比对目标文件），是「c6 后端还能编自己」
的快速检查。

`cmake --build build --target c99selfhost-tcc` 是同一条路，但宿主编译器就是 tcc 自己：整条链不需要 gcc。生成的 C 里有
两处就是为它存在的（tcc 不是 GNU C 编译器）：

- `va_list` 遍历器叫 `__c99_va_arg`，不叫 `__va_arg`——后者 tcc 自己在 `include/tccdefs.h` 里声明；
- 方法的原始源码名（`CRect_c6_area` 的 `c6_area`）只在 GCC/clang 下用 GNU `alias` 属性导出；
  在 tcc 下改发一个小的转发函数，别的编译单元照样能调用原始名。

导入的 libc 名字靠「私有标识符 + `__asm__` 标签」绕开宿主头文件；tcc 也支持 asm 标签，
所以只有 `alias` 属性需要额外那条路。
