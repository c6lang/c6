# c6c `-run` JIT：清单（todo）

> **状态**：📝 待评审 · **计划**：[plan.md](plan.md) · **设计**：[design.md](design.md)
> 规则照仓库惯例：**一项一个提交**，提交信息 `c6c: J-N <一句话>`，正文附验收命令与实测数字；
> 每完成一项把状态改成 ✅ 并在同一提交里更新本文件。
> 状态：⬜ 未开始 / 🚧 进行中 / ✅ 完成 / ⛔ 阻塞

---

## 总表

| ID | 内容 | 状态 | 门禁（一条命令 + 期望） |
|---|---|---|---|
| J-1 | 基线：把 plan §1 的数字重测一遍记进本文档 | ⬜ | 表格填满，数字与 plan §1 同量级 |
| J-2 | `PA_BASE` 变量化（8 处替换，默认 `0x400000`） | ⬜ | `selfhost` fixpoint 逐字节相同；`-o` 产物新旧编译器逐字节相同 |
| J-3 | `src/jit.c6` 骨架 + 固定窗 Image（镜像直接长在窗里，**不拷贝**） | ⬜ | `-run` 跑通 hello（argv + 退出码）；`-run` 不慢于 `-o`+直接跑 |
| J-4 | `x64_emit_start_jit`（ctors / main / dtors / `ret`，先不管 `%fs`） | ⬜ | ctors/dtors 顺序用例；`-test` 通 |
| J-5 | 驱动接线（`main.c6`：M_RUN 与无 `-o` 的 `-test`） | ⬜ | `run-flag.sh` 全过；`TMPDIR=/nonexistent` 也能跑 |
| J-6 | 新回归 `tests/jit-run.sh`（含 exec 差分、CTest 目标 `jitrun`） | ⬜ | 脚本自报 `通过 N / 失败 0` |
| J-7 | TLS / `%fs`：`rt.c6` 两个 helper + 桩里保存/恢复 + `NTLS>0` 门控 | ⬜ | TLS 用例与 exec 路径输出一致；stage0（glibc 宿主）下也一致 |
| J-8 | JIT 私有堆：`c6c_brk_cur/left` 去 `static` + 装载器装 arena | ⬜ | 8 MB `malloc` 用例通过；宿主之后的 `malloc` 不受影响 |
| J-9 | 诊断与逃生口：`C6C_RUN`、`C6C_JIT_CAP`、五条错误消息 | ⬜ | 超限 / 未知取值 / `exec` 三条用例 |
| J-10 | 大镜像与自编译自跑 | ⬜ | `-run src/c6c.c6` 打出 usage；13.7 MB 镜像装得下 |
| J-11 | 全量门禁 | ⬜ | `fast`、`check-all-c6c`、`selfhost`、`runflag`、`testflag`、`bcheck`、`embedcheck` 全绿 |
| J-12 | 性能/开销数据（前后各 9 次中位数 + 50 次批量，含 13.7 MB 大镜像） | ⬜ | `-run` ≤ `-o`+直接跑；数字写进 README 与 acceptance |
| J-13 | 文档收口（README §-run、design §13、todo 轮次、acceptance 行） | ⬜ | 文档与实现一致，链接不倒 |
| J-14 | （可选）`embed.c6` 的 `c6_run` 换成本轮装载器 | ⬜ | `embedcheck`、`pkgcheck` 全过 |
| J-15 | （可选）`X64Sym.va` → `u64`，去掉 < 2 GiB 限制 | ⬜ | 同 J-2 的门禁（产物逐字节不变） |
| J-16 | （可选、与本轮解耦）`rt.c6` 的 `memcpy`/`memset` 字号化 | ⬜ | 吞吐基准复测；`selfhost` 与全部回归不变 |

---

## J-1 基线

把 plan §1 那张表按当前 `main` 重测，结果直接写进本节（命令也写进来，便于复查）。

```sh
C6C=build/c6c; W=$(mktemp -d)
printf '#include <stdio.h>\nint main(void){printf("hi\\n");return 7;}\n' > $W/h.c6
printf 'int main(void){return 7;}\n' > $W/r7.c6
strace -f -e trace=execve,unlink,openat $C6C -run $W/h.c6 2>&1 | grep -E 'c6c-run|execve'
TMPDIR=/nonexistent-dir $C6C -run $W/h.c6; echo rc=$?     # 期望 rc=2
$C6C $W/h.c6 -o $W/h && stat -c%s $W/h                    # 442888
$C6C c6c/src/c6c.c6 -o /tmp/c6c-probe && stat -c%s /tmp/c6c-probe   # 13684120
```

耗时用 Python 的 `time.perf_counter()` 取 9 次中位数（没有 `/usr/bin/time`）：
`-o` 55.0 ms / `-run` 55.8 ms / 产物裸跑 0.35 ms；50 次批量 `-run` 57.95 ms/次 vs
`-o`+直接跑 57.16 ms/次。**本机 `TMPDIR` 放 tmpfs 与放仓库磁盘没差别**（56.4 / 56.6 ms），
所以"临时文件慢"不是本机的论据（plan §9.4）。13.7 MB 大镜像：`-o` 583.8 / `-run` 583.1 ms。

**已知最大镜像**：c6c 自编译 13,684,120 B（代码 3,826,177 B / 5,789 符号）
→ 预留窗 64 MB 是它的 4.7 倍。

## J-2 `PA_BASE` 变量化

按 design §11.2 的 8 处替换；`src/elf.c6` 里新增 `static u32 PA_BASE = ELF_BASE_VA;`，
写 ELF 头的三处**保持** `ELF_BASE_VA`。

验收：

```sh
cmake --build build --target selfhost        # fixpoint：c6c2 与 c6c3 逐字节相同
# 产物不许变：新旧编译器对同一批源码 -o 出来的文件逐字节相同
for f in c6c/tests/*.c6 c6c/src/c6c.c6; do
  build-old/c6c $f -o /tmp/a 2>/dev/null && build/c6c $f -o /tmp/b 2>/dev/null && cmp /tmp/a /tmp/b || echo "DIFF $f"
done
```

`build-old/c6c` 就是这一项之前的编译器（提交前先拷一份留着）。**有任何一个 DIFF 就回退重做。**

## J-3 `src/jit.c6` 骨架 + 固定窗 Image

**不要写"编完 memcpy 进窗"的版本** —— 实测那是负收益（plan §9.2 / design §5.1：
`rt.c6` 的 `memcpy` 是字节循环，443 KB 要 5.3 ms，比要省下的 0.8 ms 还多）。
镜像直接长在窗里。

- `jit_begin()`：`mmap(NULL, cap, PROT_READ|PROT_WRITE,
  MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT|MAP_NORESERVE, -1, 0)`；
  `#ifndef MAP_32BIT / #define MAP_32BIT 0x40 #endif`；设 `PA_BASE`；断言 `base + cap < 2^32`。
- `elf.c6`：`Image` 加 `fixed` 标志 + `img_attach(im, p, cap)`；`img_grow` 在 `fixed` 时
  只做 `need <= cap` 检查（报错文案："JIT 窗不够：数据段/代码段需要 N MB"），
  **不 `realloc`、不 `memset`**（匿名页本来就是零）。
- `sym.c6:89` 与 `pa_setup` 里那两处 `img_init`：JIT 时改成
  `img_attach(&DATA_IMG, (u8 *)PA_BASE + ELF_HDR, JIT_CODE_OFF - ELF_HDR)` /
  `img_attach(&CODE_IMG, (u8 *)PA_BASE + JIT_CODE_OFF, JIT_CAP - JIT_CODE_OFF)`，否则照旧。
  数据段超了 `cap/2 - 120` 就报错并提示调大 `C6C_JIT_CAP`（两边各占一半，一起变大）。
- `jit_code_off()`：返回 `JIT_CAP / 2`（默认 32 MB，页对齐）—— 解析前就定死；`pa_finish` 里
  `code_off = JIT_ON ? jit_code_off() : (ELF_HDR + DATA_IMG.n)`。
- `jit_protect(entry_off)`：`used > cap` 报错；数据段 → RW、洞 → `PROT_NONE`、代码段 → RX
  （三次 `mprotect`，页对齐，一个字节都不搬）。
- `jit_sym(name)`：按 `x64_sym_hash` + `X64_SYM_HEAD` 查（照 `embed.c6` 的 `c6_get_symbol`）。
- `jit_run(argc, argv)`：`fflush(0)`（宿主 stdio；`rt.c6` 的 `fflush` 是空实现）→
  调 `(int (*)(int, char **))(base + entry_off)` → 返回 rc。

验收：

```sh
build/c6c -run $W/r7.c6; echo rc=$?          # 7
build/c6c -run $W/h.c6                       # 有 hi
build/c6c -run c6c/src/c6c.c6; echo rc=$?    # 13.7 MB 镜像装得下 → 打 usage，rc=2
build/c6c/tests/jit-run.sh --bench           # J-12 加的：要求 JIT 不慢于 exec 路径
```

## J-4 入口桩

design §6 的指令序列；`with_fs` 先固定传 0（J-7 再接上）。
桩必须设 `st->entry`；`sub rsp,40` 之后的栈对齐用 `printf("%d")` 这类吃栈的函数压一压。

验收用例（临时脚本，J-6 收进正式脚本）：

```c
/* ctors/dtors 顺序：应当先 A 后 B，退出前先 D 后 C */
#include <stdio.h>
__attribute__((constructor)) static void c1(void) { printf("A\n"); }
__attribute__((constructor)) static void c2(void) { printf("B\n"); }
__attribute__((destructor))  static void d1(void) { printf("C\n"); }
__attribute__((destructor))  static void d2(void) { printf("D\n"); }
int main(void) { printf("main\n"); return 3; }
```

## J-5 驱动接线

design §11.5。**注意**：`-run` 成功时不能出声（`run-flag.sh` 第 1 条断言 stdout 为空）；
`-test` 的 `argv[0]` 用源文件名（不再有临时路径）。

验收：

```sh
C6C=build/c6c ./c6c/tests/run-flag.sh          # 通过 15 / 失败 0（含 shebang、信号、互斥；实测基线）
C6C=build/c6c ./c6c/tests/test-flag.sh
TMPDIR=/nonexistent-dir build/c6c -run /tmp/h.c6; echo rc=$?     # 期望 0（旧路径是 2）
```

## J-6 `tests/jit-run.sh`

脚本结构与 `run-flag.sh` 对齐（`ok`/`bad` 计数、末尾 `[ "$fail" -eq 0 ]`；
另加一个 `--bench` 模式：只跑 J-12 那组批量计时，**不进** `fast` 门禁，手动/夜间跑），
CTest 侧在 `cmake/C6C.cmake` 加 `c6c_target(jitrun ENV C6C=${C6C_BIN} COMMAND ./tests/jit-run.sh)`
并挂进 `fast` / `check-all-c6c` 的 `DEPENDS`。

测试矩阵（每条都要有明确期望）：

| # | 用例 | 期望 |
|---|---|---|
| 1 | `-run` hello（printf + `return 7`） | stdout=`hi`，退出码 7，编译器自己不出声 |
| 2 | `TMPDIR=/nonexistent-dir -run` | 退出码 7（**证明不落临时文件**） |
| 3 | `strace -f -e trace=execve` | 只有 1 次 `execve`（c6c 自己）；有 strace 才跑，没有跳过 |
| 4 | argv 透传 | `argc=3 [<源文件>] [one] [-two]`，`argv[0]` 是源文件名 |
| 5 | 只有 test 块的文件 | 全过时静默且 rc=0 |
| 6 | 两块都挂的 test 块 | rc=2，两行 `FAIL test:"…"` 到 stdout |
| 7 | `raise(15)` | rc=143 |
| 8 | `exit(3)` / `abort()` | rc=3 / rc=134 |
| 9 | 编译错误 | rc≠0，无残留、不崩 |
| 10 | shebang（`#!/abs/path/c6c -run`） | rc=3，输出正确 |
| 11 | `-run -b` 越界 | rc=2，stderr 点名越界下标（与 `bcheck.sh` 第 8 条同口径） |
| 12 | **exec 差分**：同一批程序，`-run` vs `-o`+直接跑 | stdout/stderr/退出码**逐字节相同**（这轮的硬约束） |
| 13 | TLS（`__thread` 读/写/初始化器） | 输出与 exec 路径相同；**且编译器退出正常**（`%fs` 恢复的证明） |
| 14 | `pthread_create` + `pthread_join` | `thread 9` / `joined`，rc=0 |
| 15 | `malloc(1<<20)` + `memset` 再读回 | 内容正确（私有 arena 的证明） |
| 16 | 互斥检查 | 与 `run-flag.sh` 第 8 条同（`-o`/`-c`/`-fsyntax-only`/缺源文件 → rc=2） |
| 17 | `C6C_RUN=exec -run` | 与 JIT 走同一条差分（逃生口可用） |
| 18 | **性能红线**（`--bench`）：hello 50 次批量 | JIT 的 `-run` ≤ `-o`+直接跑 + 1 ms；顺带打印 13.7 MB 大镜像一次的数 |

`-run` 前后各跑一遍同一套（`C6C_RUN=jit` / `C6C_RUN=exec`），差分才可信。

## J-7 TLS / `%fs`

`lib/rt.c6` 加 `__c6_fs_get` / `__c6_fs_set`（design §7）；桩里两侧夹住构造函数与析构函数，
`with_fs = (NTLS > 0)`。

验收：

```sh
# 先造一台"rt.c6 当 libc"的宿主：c6c 编自己 → build/c6c2
build/c6c -o build/c6c2 c6c/src/c6c.c6
# TLS 程序在两种宿主下都必须正常退出、输出正确
build/c6c  -run /tmp/tls.c6; echo rc=$?     # stage0（glibc 宿主）
build/c6c2 -run /tmp/tls.c6; echo rc=$?     # 自举产物（rt.c6 宿主）
```

**必须两种宿主都试**：glibc 宿主一旦 `%fs` 没恢复，尾声的 stack protector 会立刻 abort。

## J-8 JIT 私有堆

`lib/rt.c6`：`c6c_brk_cur` / `c6c_brk_left` 去 `static`（加注释说明谁在用）；
`jit_run` 里把镜像的两个符号指到一块 `mmap(64 MB, RW, NORESERVE)` 的 arena。

验收：`malloc(1<<20)` 用例（J-6 第 15 条）+ 一个 8 MB 的用例；
外加"跑完之后宿主的 `malloc` 仍然正常"——用 `C6C_RUN=exec` 与 JIT 各跑一次大程序，
比较编译器的行为（编译器跑完就退出，所以这里主要靠代码审查与注释把关）。

## J-9 诊断与逃生口

design §12 的五条消息 + 两个环境变量。

验收：

```sh
C6C_JIT_CAP=1 build/c6c -run /tmp/h.c6; echo rc=$?          # 期望 2 + "装不下"
C6C_RUN=foo   build/c6c -run /tmp/h.c6; echo rc=$?          # 期望 2 + "不认识"
C6C_RUN=exec  build/c6c -run /tmp/h.c6; echo rc=$?          # 期望 7（老路径）
```

## J-10 大镜像与自编译自跑

```sh
build/c6c -run c6c/src/c6c.c6        # 13.7 MB 镜像，没参数 → 打 usage，rc=2
```

这条同时验证：13.7 MB 装得下、大镜像的段权限拆分正确、程序的 `exit` 路径正确。
耗时约为一次自编译（~0.3 s），可以进 `jitrun`（放最后，标一行耗时）。

## J-11 全量门禁

```sh
cmake --build build --target fast
cmake --build build --target check-all-c6c
cmake --build build --target selfhost
C6C=build/c6c ./c6c/tests/run-flag.sh
C6C=build/c6c ./c6c/tests/test-flag.sh
C6C=build/c6c ./c6c/tests/bcheck.sh
C6C=build/c6c ./c6c/tests/jit-run.sh
cmake --build build --target embedcheck
```

任何一条红就先修，不往下堆。

## J-12 性能数据

测法与 plan §9 同口径（便于直接对比）：Python `time.perf_counter()`，9 次取中位数；
批量组各 50 次。三组程序：hello、`return 7`、自编译（13.7 MB 镜像）。

要填进 README / `docs/acceptance.md` 的表：

| 场景 | `-o` + 直接跑 | `-run`（旧 exec 路径） | `-run`（JIT） |
|---|---|---|---|
| hello（443 KB 镜像） | 55.0 ms | 55.8 ms | 目标 ≤ 55.3 ms |
| 50 次批量（tiny） | 57.16 ms/次 | 57.95 ms/次 | 目标 ≤ 57.2 ms/次 |
| 自编译（13.7 MB 镜像） | 583.8 ms | 583.1 ms | 目标 ≤ 583.5 ms |

另外记：每个程序的代码段增量（`__c6_fs_get`/`__c6_fs_set` 带来的 +~50 B）。

**红线**：JIT 不得比 exec 路径慢（`--bench` 自动断言）。慢了就说明又滑回"拷贝镜像"
那条老路（plan §9.2：`rt.c6` 的 `memcpy` 0.083 GB/s，拷 443 KB 要 5.3 ms，
比要省的 0.8 ms 还多）。

**平台差异不要写成一刀切**：本机 `TMPDIR` 在 tmpfs 上，exec 路径的临时文件几乎不要钱
（56.4 vs 56.6 ms）；换到只读根 / `seccomp` / 慢文件系统，旧路径是失败或几十毫秒抖动。
文档里要分开说（plan §9.4）。

## J-13 文档收口

| 文件 | 改什么 |
|---|---|
| `c6c/README.md` §直接跑 c6 源码 | 改成"进程内 JIT"，写清 `C6C_RUN=exec` 逃生口与行为差异表（design §10） |
| `c6c/docs/design.md` §13 开放问题 1 | 改成"已做，进程内镜像 + mmap，见 docs/jit/design.md" |
| `c6c/docs/acceptance.md` | 新增一行 `-run`（JIT）的验收命令与实测 |
| `c6c/docs/todo.md` | 加一条轮次记录（草稿见下） |
| `c6c/docs/jit/{plan,design,todo}.md` | 状态从 📝 待评审 改成 ✅ 已实现 |

轮次记录草稿：

```
## 第 N 轮：`-run` 换成进程内 JIT

硬缺口：`-run`/`-test` 一直走"临时 ELF + fork/exec"，没有可写 TMPDIR 就跑不了
（实测 rc=2），也挡住库形态（embed.c6 把镜像装到 0x400000，而 c6c 自己就在那儿）。

改法：基址变量化（PA_BASE，8 处）→ 解析前 mmap 预留 32 位窗 → 收尾把
[数据段][代码段] 拷进窗、拆 RW/RX → 调镜像里的入口桩（ctors/main/dtors/ret）。
`%fs` 在镜像内保存-恢复；JIT 程序用私有 mmap arena，不推宿主的 brk。

实测：……
```

## J-14（可选）库形态换装载器

`embed.c6` 的 `c6_relocate` / `c6_run` 改成用 `jit.c6`，删掉 `0x400000` 固定装入。

验收：`embedcheck`、`pkgcheck` 全过；另外用**自举产物**（`build/c6c2`）当宿主跑一次
`c6_compile_string` + `c6_run`（现在这条路必然失败），拿到 42。

## J-15（可选）`X64Sym.va` → `u64`

去掉 < 2 GiB 限制，`PA_BASE` 用普通 `mmap`（不再需要 `MAP_32BIT`）。

验收：同 J-2（产物逐字节不变）+ `selfhost`。

## J-16（可选）`rt.c6` 的 `memcpy`/`memset` 字号化

与本轮解耦（本轮**不依赖**它，也正是 design §5.1 的结论），但它是自举编译器的一大块开销：

- `lib/rt.c6:241` `c6c_mem_copy`（= `memcpy`）逐字节：实测 **0.083 GB/s**（glibc 7.96）；
- `lib/rt.c6:1709` `memset` 逐字节：实测 **0.10 GB/s**；
- 两者都在编译热路径上：`img_grow`（`realloc` → 拷贝）、`data_alloc`（每次分配都 `memset`）。
- 参照：自举版 `build/c6c2` 自编译 2567 ms，gcc 版 stage0 549 ms（4.7×；其中多少来自
  这两个函数还没拆过，值得先测再改）。

做法：按 `long` 对齐头尾 + 中间 8 字节块（c6c 的 codegen 是槽式的，实测 8B 块循环 0.62 GB/s、
32B 0.69 GB/s；要更快就得靠 asmblob 的 `rep movsb`，那需要单独评审）。

门禁：`selfhost` fixpoint、`fast`、`check-all-c6c`、`runflag`、`testflag`、`bcheck` 全绿
（产物会变，但 fixpoint 比的是一致的自举链，不受影响）；吞吐用同一条 1 GiB 基准复测。
