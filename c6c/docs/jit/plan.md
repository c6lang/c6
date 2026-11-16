# c6c `-run` 的 JIT 实现：计划

> **状态**：📝 待评审 · **设计**：[design.md](design.md) · **清单**：[todo.md](todo.md)
> 相关：现有设计 [../design.md](../design.md) §13.1（开放问题 1）、现有 README [§直接跑 c6 源码](../../README.md)

## 0. 一句话

`-run`（以及"没给 `-o` 的 `-test`"）现在是「编到临时 ELF → `fork` + `execve` → 删临时文件」。
这一轮把它换成 tcc `tcc_run` 那套：**编到进程内镜像 → `mmap` → 直接 call 入口**。
`-o` / `-c` / `-S` / `--emit-c` / `--emit-llvm` 五条路的产物**一个字节都不动**（这是硬门禁）。

## 1. 现状（本机实测，全部可复现）

| 观察点 | 实测 | 怎么量的 |
|---|---|---|
| `-run` 一定落一个临时文件 | `/tmp/c6c-run-XXXXXX`；hello 442,888 B、自编译镜像 13,684,120 B | `strace -f -e trace=execve,unlink,openat` |
| `-run` 一定 fork + exec | 两次 `execve`（c6c 自己 + 临时 ELF），之后 `unlink` | 同上 |
| 没有可写 TMPDIR 就跑不了 | `TMPDIR=/nonexistent-dir c6c -run x.c6` → `rc=2`，`c6c: error: 建不了临时文件 ...` | 直接跑 |
| `-run` 相对 `-o` 的额外开销 | hello：`-o` 55.0 ms / `-run` 55.8 ms（产物裸跑 0.35 ms） | Python `perf_counter`，9 次中位数 |
| 镜像构成 | hello：代码 287,852 B / 镜像 442,888 B；c6c 自编译：代码 3,826,177 B / 5,789 符号 / 镜像 13,684,120 B | `c6c -o` 的报告行 + `stat` |
| c6c 自己是 **ET_EXEC、装在 0x400000** | `readelf -h c6c/build/c6c`：类型 EXEC，入口 `0x41e840` | `readelf` |

两条推论，直接决定了设计（详见 [design.md](design.md) §2、§4）：

1. c6c 自己的映像就占着 `0x400000` 起的两兆多 —— 所以 `src/embed.c6` 现在那套
   `MAP_FIXED_NOREPLACE(0x400000)`（`c6_relocate`）在自举产出的编译器里**必然失败**。
   `-run` 不能沿用固定基址。
2. 镜像里所有绝对地址都是**解析期**就烧进去的（`DATA_VA` 宏，见 design §2.2），
   所以"先编完再搬到别处"这条路要重定位表，代价远大于"解析前先把基址定下来"。

## 2. 为什么值得动

按重要性排：

1. **`-run` 不该要一个可写的 TMPDIR**：只读根文件系统 / 容器 / CI 沙箱里现在这条直接死
   （实测 `rc=2`）。这一条是"能不能跑"，不是"快不快"。
2. **`-run` 不该 fork + exec**：`seccomp` 禁 `execve` 的环境一跑就挂；顺带省下
   临时文件 + fork/exec 的 ~0.8 ms（占一次 `-run` 的 1.4%，见 §9 —— 速度不是主诉求）。
3. **库形态被固定基址卡住**：`src/embed.c6` 的 `c6_*` / `tcc_*` API 把镜像装到 `0x400000`，
   而 c6c 自己就在那儿。换成"任意基址"之后，库形态和 `-run` 共用同一套装载器
   （后续可把 `c6_run` 也搬过来，见 [todo.md](todo.md) J-14）。
4. **和 tcc 对齐**：`tcc -run` 就是 mmap + 重定位（`tccrun.c`）。c6c 要顶替 tcc，
   这一条早晚要有；现在补上，语义、诊断、文档一次说清。

## 3. 不做什么（先划界，避免范围失控）

| 不做 | 原因 |
|---|---|
| 动态链接 / `dlsym` 解析未定义符号 | c6c 的产物是自带运行时的静态镜像，`-run` 也一样；未定义符号仍按现有方式报错（`x64_resolve` 的 `exit(2)`） |
| 优化器 / 热点重编译 / JIT 缓存 | 这是"装进去就跑"，不是 HotSpot；c6c 明确没有优化器 |
| aarch64 后端 | `--target=aarch64` 现在只支持 `-S`（出汇编文本），`-run` 仍拒绝 |
| C99 / LLVM 文本后端 | 现在是"生成文本再靠外部编译器"，`-run` 本来就不支持（`parse_args` 已拒绝） |
| 一个进程里重复编多次 | 编译器的全局状态（符号表 / 代码镜像 / 预处理栈）是"一次编译一个程序"（embed.c6 的边界） |
| 撤掉 exec 代码路径 | 留着当逃生口与差分对照（`C6C_RUN=exec`），见 design §12 |

## 4. 里程碑

| 里程碑 | 范围 | 产出 | 门禁（绿了才进下一档） |
|---|---|---|---|
| **M0** 本文档 | 计划 / 设计 / 清单三份 + 基线数字 | `docs/jit/{plan,design,todo}.md` | 评审通过 |
| **M1** 基址可变量化 | `ELF_BASE_VA` → `PA_BASE`（默认 `0x400000`），parse/x64/asmx 共 8 处 | 无行为变化 | `selfhost` fixpoint 逐字节相同；`-o` 产物与新编译器逐字节相同（corpus） |
| **M2** 装载器 + 入口桩 | `src/jit.c6`（预留/固定窗/`mprotect`/调用）、`x64_emit_start_jit` | `-run` 在进程内跑起来 | hello / 退出码 / argv / `-test` 全过 |
| **M3** 运行时卫生 | `%fs` 保存-恢复、JIT 私有堆 | TLS 与 malloc 用例和 exec 路径同构 | `tests/jit-run.sh` 的 TLS / 堆 / 线程三条 |
| **M4** 验收与收口 | `tests/jit-run.sh`（含 exec 差分）+ 全量门禁 + 文档 | 默认 JIT，`C6C_RUN=exec` 逃生 | `fast` / `check-all-c6c` / `selfhost` / CTest 全绿 |
| **M5**（可选） | `embed.c6` 换成 `jit.c6`；`X64Sym.va` 放宽到 u64 | 库形态不再依赖 `0x400000` | `embedcheck` / `pkgcheck` 全过 |

## 5. 风险与对策

| # | 风险 | 对策 / 门禁 |
|---|---|---|
| R1 | `PA_BASE` 的改动漏进 `-o` 产物，自举 fixpoint 漂 | 默认值 `0x400000`；`elf.c6` 的 ELF 头常量**不**跟着变；M1 的门禁就是"逐字节相同" |
| R2 | `X64Sym.va` 是 `u32`，基址上界 4 GiB | `MAP_32BIT`（内核保证 < 2 GiB）+ 装填前断言 `base + used < 2^32`，越界明确报错 |
| R3 | JIT 程序改 `%fs` 后宿主回不来 | 保存-恢复**在 JIT 镜像里做**（桩在 `ret` 前恢复），宿主一返回就已经是自己的 `%fs`；由 stage0（glibc 宿主）的 TLS 用例覆盖 |
| R4 | JIT 程序的 `malloc` 推进宿主 `brk`，撞 glibc 的主 arena | JIT 用私有 `mmap` arena：`rt.c6` 的 `c6c_brk_cur/left` 去 `static`，装载器把它指到私有块；`malloc` 热路径不加分支 |
| R5 | 程序留下还在跑的线程，撤映射给它发段错误 | v1 **不 `munmap`**（进程退出时内核回收），理由写在 design §9 |
| R6 | 信号 / `exit_group` 语义变了 | 用例覆盖：`raise(15)` → 143、`exit(3)` → 3、`abort()` → 134；`run-flag.sh` 原样保留 |
| R7 | 镜像超过预留（64 MB） | 明确报错并给出需要/预留的字节数；`C6C_JIT_CAP` 可调（也用于测试） |
| R8 | 只在 x86-64/Linux 成立 | `-run` 本来就只支持 native 后端；`MAP_32BIT` 是 x86-64 的，`#ifndef` 兜底 + 注释 |

## 6. 评审要拍板的 6 件事（每件附推荐）

1. **默认走 JIT，还是先 opt-in 一轮？**
   推荐：**默认 JIT**，保留 `C6C_RUN=exec` 逃生口 + 差分脚本。理由：`runflag`/`testflag`/`bcheck`
   三条现成回归就是"对外行为契约"，默认切换的直接风险已经被它们兜住；opt-in 反而会让新路径长期没人跑。
2. **`rt.c6` 的两处小改能否接受？**（`c6c_brk_cur/c6c_brk_left` 去 `static`；
   新增 `__c6_fs_get`/`__c6_fs_set` 两个 arch_prctl helper）
   推荐：**接受**。前者对 `malloc` 热路径零改动（只是少一个 `static`），后者按仓库自己的规矩
   （能写成 C 的就不写机器码 blob）也不该进 `asmblob.c6`。代价：每个程序多两个符号、~50 B 代码。
3. **基址用 `MAP_32BIT` + 64 MB 预留，还是顺手把 `X64Sym.va` 放宽到 u64？**
   推荐：**先 `MAP_32BIT`**（u64 化是 5 个写入点 + `embed.c6` 读取点 + `u32 off` 的既有约定，
   单独一项更好评审，见 J-15）。64 MB = 已知最大镜像（自编译 13.7 MB）的 4.7 倍。
4. **行为差异清单（design §10）能否接受？** 最扎眼的是 `/proc/self/exe` 会变成 c6c、
   程序与编译器共享地址空间与栈。推荐：接受并写进 README；这些差异在 `tcc -run` 里同样存在。
5. **是否顺带把 `embed.c6` 的 `c6_run` 换成新装载器？** 推荐：**放到 M5/另一个提交**，
   本轮的验收面已经够大，`-run` 与库形态分开评审。
6. **（性能评审新增，结论已随 J-16 反转）镜像"直接长在窗里"要不要做？**
   推荐：**不做，回到"编完拷进窗"。** J-16 把 `rt.c6` 的 `memcpy` 从 0.083 GB/s
   提到 6.80 GB/s 之后，拷贝代价只剩 0.065 ms（443 KB）/ 2.0 ms（13.7 MB），
   占比 ≤0.4%（§9.2 重测），为它去改 `Image` 不值得。固定窗方案保留在
   design §5 的"备选"里（镜像再大一个数量级、或要省掉那 13.7 MB 双份内存时再上）。

## 7. 验收命令（本轮结束时要能一条条跑）

```sh
# 1. 新回归（本轮新增）
ctest --test-dir build -R 'jitrun' --output-on-failure
#    或：C6C=build/c6c ./c6c/tests/jit-run.sh

# 2. 老回归不许变（对外行为契约）
C6C=build/c6c ./c6c/tests/run-flag.sh
C6C=build/c6c ./c6c/tests/test-flag.sh
C6C=build/c6c ./c6c/tests/bcheck.sh          # 第 8 条就是 `-run -b`

# 3. 产物不许变（M1 的硬门禁）
#    旧编译器 vs 新编译器，同一批源码的 -o 产物逐字节相同；selfhost fixpoint 逐字节相同
cmake --build build --target selfhost

# 4. 全量
cmake --build build --target fast
cmake --build build --target check-all-c6c

# 5. 性能红线（plan §9）：JIT 不得比 exec 路径慢
C6C=build/c6c ./c6c/tests/jit-run.sh --bench
```

## 8. 评审通过后怎么推进

严格按 [todo.md](todo.md) 一项一个提交（`c6c: J-N <一句话>`，正文附验收命令与实测数字），
每项做完在同一提交里把 todo 的状态改 ✅。任何一项门禁没绿就停下来先修，不往下堆。

## 9. 性能评审（实测）

> 这一节是**对着量出来的数字**评审的，结论改了两处设计：镜像不再"编完再拷"，
> 而是"直接长在窗里"（design §5）；`-run` 的收益也按实测重新写小。

### 9.1 拆开看：`-run` 比 `-o` 多的 0.8 ms 是什么

| 段 | 耗时 | 怎么量的 |
|---|---|---|
| 编译本身（含 `rt.c6`） | 55.0 ms | `-o` 全流程 |
| 临时文件 + fork/exec + 产物启动（合计） | **0.8 ms** | `-run` − `-o`，hello，9 次中位数 |
| 其中：产物裸跑 | 0.35 ms | 直接执行产物 |
| 50 次批量（`return 7` 小程序） | 57.95 vs 57.16 ms/次 | 差 0.8 ms，与单次一致 |
| 大镜像（13.7 MB，自编译） | `-o` 583.8 / `-run` 583.1 ms | 差在噪声里（≤0.5%） |

### 9.2 拷不拷：J-16 之前是致命的一条，之后随便

按最初的设计（收尾时 `memcpy` 两个 Image 进窗），**拷贝速度就是成败**：

| 拷贝手段 | 443 KB（hello） | 13.7 MB（自编译） |
|---|---|---|
| `rt.c6` 的 `memcpy`（= 自举版编译器用的那个） | **5.3 ms** | **165 ms** |
| c6c 自己写的 8 字节块循环 | 0.71 ms | 22 ms |
| c6c 自己写的 32 字节块循环 | 0.64 ms | 20 ms |
| glibc `memcpy`（stage0 用的） | 0.06 ms | 1.7 ms |

实测吞吐：`rt.c6` 的 `memcpy` **0.083 GB/s**、8B 循环 0.62、32B 循环 0.69、
glibc **7.96 GB/s**（c6c 的 codegen 是槽式的，循环体每次过栈，怎么手写都上不去）。

对照"要省下的 0.8 ms"：用 `rt.c6` 的 `memcpy` 拷贝 = **净亏 4.5 ms（+8%）**；
就算手写 32B 循环也只是打平（0.64 vs 0.8 ms）。

**当时的结论**：镜像**直接长在那块窗里**（固定窗 Image，不拷贝）。
**J-16 落地后重测，这个结论反过来了**：

| | 443 KB（hello） | 13.7 MB（自编译） |
|---|---|---|
| 新 `rt.c6` 的 `memcpy`（`rep movsb`） | **0.065 ms** | **2.0 ms** |
| 占一次 `-run` 的比例 | 0.12% | 0.34% |

也就是说"编完拷进窗"现在只值 0.12%–0.34% 的开销，**不再值得为它改 `Image`**
（`img_attach` + 固定窗分支 + `code_off = cap/2` 的整套约束）。design §5 因此
改回最简单的做法：编译照旧长在堆里的两个 `Image`，收尾一次 `__c6_memcpy_rep`
拷进窗、两次 `mprotect`、调入口。固定窗方案降级为"备选"，写在同一节里。

### 9.3 改完之后 JIT 自己剩下的开销

| 项 | 次数/规模 | 量级 |
|---|---|---|
| `mmap` 预留 + 私有堆 arena + 2–3 次 `mprotect` | 4–5 次系统调用 | ~4–10 µs |
| `arch_prctl` GET_FS / SET_FS（仅带 `__thread` 的程序） | 2 次 | ~1–2 µs |
| 首触页（只碰真正写过的页；窗是 `NORESERVE`） | 与现在写 malloc 缓冲同级 | ≈0 |
| 编译器自身热路径（`PA_BASE` 从常量变静态变量） | 8 处，全在冷路径（每个函数 / 每个数据符号） | <0.1% |

**能省下的**：0.8 − 0.065（拷贝）− ~0.01（系统调用）≈ **0.72 ms / 1.3%**
（hello 55.8 → 55.1 ms 量级）。也就是：本轮**不是性能优化**，但必须不是负收益 ——
9.2 那条就是红线。

### 9.4 也如实说：这台机器上"临时文件"并不贵

`TMPDIR=/tmp`（tmpfs）vs `TMPDIR=<仓库所在磁盘>`：56.4 / 56.6 ms —— **没有差别**。
443 KB 的写在页缓存里被吸收掉了。所以"去掉临时文件会更快"在本机**不成立**；
它的价值是"**没有可写 TMPDIR 时也能跑**"（现在 `rc=2`）和"不再依赖 `execve`"。
换到只读根文件系统 / `seccomp` / 网络文件系统的 TMPDIR，旧路径是直接失败或几十毫秒级
抖动，那才是这两条真正的收益场景。

### 9.5 顺带发现 → 已单独做完（J-16）

`rt.c6` 的 `memcpy` / `memset` 原来都是**逐字节循环**（实测 0.083 / 0.10 GB/s，
glibc 的 `memcpy` 7.96 GB/s）。`memcpy` 已经先做完（todo J-16）：

- 走 `src/asmblob.c6` 里新加的 10 字节 `rep movsb` 跳板，1 GiB 吞吐 **0.083 → 6.80 GB/s**
  （gcc 水平的 85%）；443 KB 拷贝 5.3 ms → 0.065 ms。
- 小拷贝（<16 B）仍走 C 的 8 字节块循环：`rep` 有约 14 ns 启动开销。
- **编译速度**（硬约束）：stage0 `-o` hello −1.2%、自编译 −0.8%（持平）；
  **自举版 c6c2 自编译 2684 → 2070 ms（−22.9%）**。
- `memset` 还是字节循环（0.10 GB/s），`data_alloc` 每次分配都要 `memset`，
  量级可能比 `memcpy` 更大 —— 留给下一项（`rep stosb`，同一套办法）。
