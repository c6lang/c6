# c6c `-run` 的 JIT 实现：设计

> **状态**：📝 待评审 · **计划**：[plan.md](plan.md) · **清单**：[todo.md](todo.md)
> 涉及的代码位置按评审时的 `main`（`18d90ea`）标注；实现时行号会漂，按函数名找。
> 现有设计里的对应条目：[../design.md](../design.md) §13 开放问题 1（"JIT 式装载器仍不做"）。

## 0. 结论先写

`-run` / `-test`（无 `-o`）改成：

```
解析期 ── 先 mmap 预留一块 <2 GiB 的地址窗（PA_BASE = 窗首）
  ↓
收尾（pa_finish）── 和现在完全一样地打补丁、补跳板，只是把 ELF_BASE_VA 换成 PA_BASE
  ↓
不落盘：把 [数据段][代码段] 拷进那块窗（一次 `rep movsb`，443 KB 只要 0.065 ms，见 §5）
  ↓
mprotect 拆成"数据 RW / 代码 RX / 中间的洞 PROT_NONE" → 调镜像里的入口桩
```

三件事一句话说完：**基址变成解析前定下来的变量**、**镜像直接长在进程内的窗里
（只拷一次，理由见 §5.1）**、**入口桩替代 `_start`
（唯一的语义差别是"返回"而不是 `exit_group`）**。

## 1. 现在的流水线：镜像怎么长出来的

`pa_finish`（`src/parse.c6`）里 native 后端的那一段，是所有"地址"的最终裁决者：

```
① main_sym = PA_MAIN->val;  entry_off = CODE_IMG.n;
② x64_emit_start(&X64, main_sym, CTORS, NCTOR, DTORS, NDTOR)   ← _start 追加在代码尾
③ code_off = ELF_HDR + DATA_IMG.n                              ← 120 + 数据段长度
④ x64_finish_label_addrs(&X64, ELF_BASE_VA + code_off)          ← `&&label` 的 imm64
⑤ CLBL[] 回填（数据段里指向代码的 `&&label`）
⑥ for sym: off += code_off（代码符号）；sym.va = ELF_BASE_VA + sym.off
⑦ data_fixups_apply()                                          ← 数据段里的符号地址
⑧ img_slack(&CODE_IMG, 64 KB); X64.im_base = code_off
⑨ x64_resolve(&X64)                                            ← 打 rel32 + 按需补 asmblob 跳板
⑩ elf_write_parts(out, hdr, &DATA_IMG, &CODE_IMG, entry_off + code_off)  ← JIT 什么都不做
```

镜像布局（`src/elf.c6`）：`[ELF 头 120][数据段][代码段]`，`VA = 基址 + 偏移`。
代码里的两种绝对引用分别由 ④⑤⑦ 回填；`rel32` 那类（`call` / `lea rip+disp32`）在 ⑨
里用**偏移**算（`target - next`），**与基址无关**——这也是为什么改基址只需要动 8 处（§11.2）。

`-run` 现在就是"把 ⑩ 的目标换成 `$TMPDIR/c6c-run-XXXXXX`，再 `fork`/`execve` 它"
（`src/main.c6` 的 `run_make_tmp` / `run_elf`）。

## 2. 三条硬约束（决定了整个设计）

### 2.1 `X64Sym.va` 是 `u32`

`src/x64.c6:21`：`u32 va; /* VA = ELF_BASE_VA + off */`。所以**基址必须 < 4 GiB**。
这不是能"顺手改大"的小事：`va` 的写入点有 7 处、读取点 3 处，`embed.c6` 还直接拿它当函数指针用。
v1 的选择是承认这条约束（见 §4），把 u64 化留给后续（[todo.md](todo.md) J-15）。

### 2.2 基址在**解析期**就烧进数据段

`src/parse.c6:7153`：

```c
#define DATA_VA(off) (ELF_BASE_VA + ELF_HDR + (off))
```

它在解析期被至少 5 处直接用掉，把**绝对地址**写进数据段：

| 位置 | 写什么 |
|---|---|
| `parse.c6:11422` | f-string 的格式串缓冲地址（`irfn_kconst`） |
| `parse.c6:14351` | 全局指针初始化器里的字符串字面量地址（`char *p = "abc";`） |
| `parse.c6:15304` | `{ (char *)"html", … }` 强转过的字面量地址 |
| `parse.c6:19545` | 复合字面量 / 数组初始化器里的字面量地址 |
| `parse.c6:20638` | TLS 影像指针 `__c6_tls_img` 的地址 |

结论：**"编完再搬"不行**。搬的话得有一份"所有绝对地址位置"的表，而上面这 5 处
根本没登记（现有登记只有 `X64.fix` / `DFIX` / `CLBL` / `labs` 四类）。
所以基址必须在**解析开始前**就定下来。

### 2.3 单次编译、单进程

编译器的全局状态（`DATA_IMG` / `CODE_IMG` / `X64` / 符号表 / 预处理栈）是"一次编译一份"
（`src/embed.c6` 的边界说明写的就是这条）。`-run` 每次进程只编一个程序，所以
"镜像 → 映射 → 调用"这一串没有生命周期问题。

## 3. 架构：`src/jit.c6` 的五个动作

```
jit_begin()     解析前：mmap 预留窗（PROT_NONE|NORESERVE|MAP_32BIT，64 MB）→ PA_BASE = 窗首
jit_fill()      收尾时：mprotect 前段 RW → 两次 __c6_memcpy_rep 装 [数据段][代码段]
                → 数据段 RW / 代码段 RX → 记入口
jit_sym()       按名字取镜像里的符号地址（数据/代码符号的 va 都已定好）
jit_run()       调入口：fflush(宿主) → 装私有堆 → call → 返回退出码（不 unmap）
jit_abort()     编译失败/装不下：munmap 整块窗
```

状态只有一个结构：

```c
typedef struct JitState {
    u8 *base;        /* 预留窗首；PA_BASE = (u32)(u64)base */
    u32 cap;         /* 预留字节数（默认 64 MB，C6C_JIT_CAP 可改） */
    u32 code_off;    /* 代码段在镜像里的偏移（= 120 + 数据段长度，收尾时才有） */
    u32 entry_off;   /* 入口桩在窗里的偏移（= code_off + 代码镜像里的偏移） */
    int on;
} JitState;
```

## 4. 基址：为什么必须 rebase、怎么选

**不能沿用固定基址。** `src/embed.c6` 现在这么干：

```c
s->mapped = mmap((void *)ELF_BASE_VA, s->image_size, R|W|X,
                 MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
```

而 c6c 自己就是 `0x400000` 起的 `ET_EXEC`（实测 `readelf -h build/c6c`：类型 EXEC、
入口 `0x41e840`、映像 0x400000..0x61xxxx）。**自举产出的编译器里这段必然失败**
（`MAP_FIXED_NOREPLACE` 撞上自己）。`-run` 要能跑全部程序（包括自举产物），
就只能选"任意基址 + 基址在编的时候就对"。

**选法**：解析前 `mmap` 一块窗，用它的地址当 `PA_BASE`。

| 要点 | 取值 | 理由 |
|---|---|---|
| flags | `MAP_PRIVATE\|MAP_ANONYMOUS\|MAP_32BIT\|MAP_NORESERVE` | `MAP_32BIT` 保证落在 < 2 GiB（`u32 va` 的上限），且不用 `MAP_FIXED`，不会撞上已经占了的区间；`NORESERVE` 让未触碰的页不占 commit |
| prot | 先 `PROT_NONE` | 解析期间谁都不写它；装填时才开 RW，代码页在调用前换成 RX（§5） |
| 容量 | **64 MB**（`C6C_JIT_CAP` 可调） | 已知最大镜像是 c6c 自编译的 13,684,120 B（4.7×）；普通程序是几百 KB |
| 超限 | 明确报错，不静默截断 | 报"需要 N 字节 / 预留 M 字节"并提示 `C6C_JIT_CAP` 与 `C6C_RUN=exec` |
| 上界断言 | `base + used < 2^32` | 防 `u32 va` 悄悄截断（`MAP_32BIT` 已经保证，断言是保险） |

被否掉的备选方案：

| 方案 | 否掉的原因 |
|---|---|
| 沿用 `0x400000` 固定基址 | c6c 自己是那儿的 ET_EXEC（§4 开头） |
| 按 `0x400000` 编完，装载时整体 +delta 搬迁 | 要一份完整的绝对地址表，而 §2.2 那 5 处没登记 |
| 每段独立 `mmap`、各自定基址（tcc 那样） | c6c 的代码用 `lea rip+rel32` 与 imm64 两种绝对引用，段间距离必须 < 2 GB 且互相可见；单块窗最接近现有 ELF 布局，补丁代码一行都不用改。**注意**：单块窗内部仍分"数据在低处、代码在 +32 MB"，那是为了 `code_off` 能在解析前定死（§5.1），不是两段 mmap |
| 编完拷贝进窗 | **就是本设计**（J-16 之后一次 `rep movsb` 只要 0.065 ms，见 §5.1） |
| 镜像直接长在窗里（固定窗 Image） | 能省掉那 0.065 ms，但要改 `Image`、`code_off` 得写成 `cap/2`、数据段还受限 —— 不划算，留作备选（§5.4） |
| 编完之后再 `mmap`（不预留） | 基址必须在解析期已知（§2.2） |
| 编两遍（先量尺寸再定基址重编） | 单趟流式设计，等于把编译时间翻倍 |

## 5. 装填：编完拷进窗

### 5.1 拷一次只要 0.065 ms（J-16 落地后的重测）

最早的设计是"收尾时 `memcpy` 两个 Image 进窗"，一量发现拷贝比要省的还贵 ——
那时的 `rt.c6` 的 `memcpy` 是逐字节循环，0.083 GB/s。**J-16 把它换成了 `rep movsb`
（6.80 GB/s）**，现在同一件事的代价是：

| 镜像 | 拷贝耗时 | 占一次 `-run` |
|---|---|---|
| hello 443 KB（数据 155 KB + 代码 288 KB） | **0.065 ms** | 0.12% |
| c6c 自编译 13.7 MB | **2.0 ms** | 0.34% |

对比"能省下的 0.8 ms"：拷一次仍然净赚 0.7 ms。所以**回到最简单的那条路**：
编译照旧长在堆里的 `DATA_IMG` / `CODE_IMG`，收尾把它们拷进窗、改权限、调入口。
不需要碰 `Image`、不需要把 `code_off` 提前定死。

（备选方案"镜像直接长在窗里"见 §5.4：它能省掉这 0.065 ms 和一份 13.7 MB 的内存，
代价是 `Image` 加固定窗模式、`code_off = cap/2`、数据段被限制在半块窗内。
现在的收益撑不起这个改动量。）

### 5.2 布局

```
base ─► ┌──────────────────────────┐
        │ 120 B  ELF 头（原样照抄）│  RW（和数据段同页）
        ├──────────────────────────┤
        │ 数据段 DATA_IMG          │  RW  ← 含字符串字面量
        ├──────────────────────────┤
        │ 代码段 CODE_IMG          │  RX  ← 含 asmblob 跳板、入口桩
        └──────────────────────────┘
base + used ─► 窗外（后面的页没映射，越界立刻 SIGSEGV）
```

`used = ELF_HDR + DATA_IMG.n + CODE_IMG.n`，窗按"先 64 MB 预留、用到多少算多少"，
所以未触碰的页既不占 commit（`MAP_NORESERVE`）也不占 RSS。

### 5.3 装填顺序（W^X）

```
jit_begin()   mmap 64 MB PROT_NONE + MAP_NORESERVE + MAP_32BIT
解析期        编译器照旧写堆里的两个 Image（一行不改）
jit_fill()    ① used = 120 + 数据段 + 代码段；used > cap → 报错（§12）
              ② mprotect(base, 页对齐(120 + 数据段), RW)
              ③ __c6_memcpy_rep(base + 120, DATA_IMG.p, DATA_IMG.n)
                 __c6_memcpy_rep(base + code_off, CODE_IMG.p, CODE_IMG.n)
              ④ mprotect(base + code_off, 页对齐(代码段), RX)
                 （数据段保持 RW；代码段从 RW 收窄成 RX）
jit_run()     调入口
```

**任何一个页都不同时是 W 和 X**：代码段先 RW 写完，再 `mprotect` 成 RX。
（`-o` 那条路的单段 RWX 这一轮不动，见 §11.7。）

`③` 直接调 `memcpy` 即可：J-16 之后它已经对 ≥16 字节的块走 `rep movsb`
（`src/asmblob.c6` 的 `__c6_memcpy_rep`）。**不要**在这里再手写 C 的逐字节/8 字节块
循环：c6c 没有优化器，那样 443 KB 要 5.3 ms、13.7 MB 要 165 ms。

### 5.4 备选（不做，留档）：镜像直接长在窗里

如果哪天镜像再大一个数量级，或者要省掉"堆里一份 + 窗里一份"的双份内存：

- `elf.c6` 的 `Image` 加 `fixed` 标志 + `img_attach(im, p, cap)`；
- `img_grow` 在 `fixed` 时只查边界，不 `realloc`、不清零（匿名页本来就是零）；
- `DATA_IMG` 挂到 `窗首 + 120`、`CODE_IMG` 挂到 `窗首 + cap/2`，
  `code_off` 因此能解析前定死（满足 §2.2），数据段被限制在 `cap/2 - 120` 以内；
- 收尾只 `mprotect`，一个字节都不搬。

收益：省掉 `jit_fill` 的那次拷贝（0.065 ms / 2.0 ms）、省一份内存、
顺带干掉 `img_grow` 里 `realloc` 的中间拷贝。成本：改 `Image`、多一条"数据段超了
`cap/2` 就报错"的约束。**J-16 之后这笔账不划算**，需要时再上。

## 6. 入口桩：`x64_emit_start_jit`

不复用 `_start`，理由只有三条，其余全照抄：

| `_start`（exec） | JIT 桩 | 原因 |
|---|---|---|
| 从 `[rsp]` 取 argc、`[rsp+8]` 取 argv（内核栈布局） | `rdi` / `rsi`（普通 C 调用） | 宿主是 `call` 进来的，不是内核 |
| 末尾 `exit_group(main 的返回值)` | `ret`（返回值在 `eax`） | JIT 要回到宿主继续跑编译器 |
| 读 `envp` 放 `rdx` | 不需要 | `rt.c6` 的环境变量走 `/proc/self/environ`（`lib/rt.c6` getenv 段），不靠 `envp` |

指令形状（`sub rsp,40` 之后 `rsp%16 == 0`，满足 SysV 的"call 前 16 字节对齐"）：

```
        sub  rsp, 40                 ; 5 个槽：[0]=argc [8]=argv [16]=宿主 fs [24]=rc
        mov  [rsp+0],  rdi           ; x_st64(im, R_RSP, 0, R_RDI)
        mov  [rsp+8],  rsi           ; x_st64(im, R_RSP, 8, R_RSI)
[仅 NTLS>0]
        call __c6_fs_get             ; x64_fixup_call（宿主 %fs 存到 [rsp+16]）
        mov  [rsp+16], rax
[ctors] call ctor[i]                 ; 源码顺序，和 _start 一致
        mov  rdi, [rsp]              ; argc
        mov  rsi, [rsp+8]            ; argv
        call main
        mov  [rsp+24], eax           ; 先存 rc，dtors 会改 rax
[dtors] call dtor[i-1]               ; 逆序，和 C 的约定一致
[仅 NTLS>0]
        mov  rdi, [rsp+16]
        call __c6_fs_set             ; 恢复宿主 %fs（必须在 ret 之前）
        mov  eax, [rsp+24]
        add  rsp, 40
        ret
```

发射用现成原语：`x_alu_ri64(im, 5, R_RSP, 40)`、`x_st64`、`x_memop(im, 0x89/0x8B, …)`、
`x64_fixup_call`、`x_ret`。桩**只碰 caller-saved**（`rax`/`rdi`/`rsi`；`syscall` 的
`rcx`/`r11`），不碰 `rbx`/`rbp`/`r12`–`r15`，所以宿主侧不需要额外保存寄存器。

桩也必须设 `st->entry`（和 `x64_emit_start` 一样），保持"镜像里总有一个入口"这条不变式。

## 7. TLS / `%fs`

事实：

- 编译期：`__thread` 变量就是数据段里的普通全局量，访问编成 `%fs + 常量偏移`；
  另外给出 `__c6_tls_img` / `__c6_tls_len` / `__c6_tls_delta`（`parse.c6` 的 `tls_finish`）。
- 运行期：`c6c_tls_setup()`（`lib/rt.c6`）mmap 一块、拷贝影像、然后
  `arch_prctl(ARCH_SET_FS = 0x1002, 块首)`。它由构造函数 `c6c_tls_init` 调，`_start`/桩在 main 前跑构造函数。
- 宿主自己也在用 `%fs`：glibc 拿它当 TCB，`c6c` 自举版拿它放自己的 TLS 块。

所以 JIT 必须"换出去再换回来"。**换回来的动作放在 JIT 镜像里、`ret` 之前**，
而不是宿主侧"回来再恢复"——后者中间隔着宿主自己的几条指令，万一宿主是
`-fstack-protector` 编的（尾声读 `%fs:0x28`）就是当场 abort。

两个 helper 加在 `lib/rt.c6`（按仓库规矩，能用 C 写的就不进 `asmblob.c6`；
`c6c_tls_setup` 里已经有同样的 `arch_prctl`，这里只是把两个方向各封一个）：

```c
/* JIT（c6c -run）：JIT 出来的程序跑在宿主进程里，%fs 是宿主在用的。
 * 程序的构造函数会把 %fs 换成自己的 TLS 块，跑完必须换回来，
 * 否则宿主一返回就是段错误。两个 helper 只是 arch_prctl：
 * GET_FS = 0x1003 / SET_FS = 0x1002。 */
void *__c6_fs_get(void) { void *p = 0; c6c_sys2(158, 0x1003, (long)&p); return p; }
void  __c6_fs_set(void *p)            { c6c_sys2(158, 0x1002, (long)p); }
```

**门控在 `NTLS > 0`**（`src/parse.c6:2261`，解析完就知道）：没有 `__thread` 的程序
根本不会换 `%fs`，一对 syscall 都省掉。代价：这两个函数随 `rt.c6` 进**每个**程序的
代码段（c6c 没有死代码消除），约 +50 B —— 记在验收里，不藏着。

## 8. 堆：为什么 JIT 程序不能推宿主的 `brk`

`lib/rt.c6` 的 `malloc` 是 brk 式：

```c
if (need > c6c_brk_left) {
    long cur = c6c_sys1(C6C_SYS_BRK, 0);        /* 当前 break */
    long got = c6c_sys1(C6C_SYS_BRK, cur + need + 1048576);
    ...
}
```

`brk` 是**进程级**的。宿主如果是 glibc 版的 c6c（stage0），glibc 的 `malloc` 假定
"从我的 arena 顶端到当前 break 之间只有我"，它会用 `sbrk()` 的返回值得出"新拿到的
那一块"从哪儿开始——JIT 程序在中间插了一块，glibc 下次扩张就会和它重叠。

**对策**：JIT 程序用一块自己的 arena，完全不碰 `brk`。

```c
/* lib/rt.c6：这两个不再是私有的 —— src/jit.c6 在跑之前把它们指到一块
 * 自己的 mmap arena 上。0 表示维持原来的 brk 路径（普通程序的路径不变）。 */
char *c6c_brk_cur = 0;
long  c6c_brk_left = 0;
```

装载器在 `jit_run` 之前：`mmap(64 MB, RW, NORESERVE)` →
`*(char **)sym("c6c_brk_cur") = arena; *(long *)sym("c6c_brk_left") = 64 MB;`。
`malloc` 的代码**一行都不用改**（`c6c_brk_left` 够大就走 bump 分配），热路径没有新分支。

arena 用尽（>64 MB）时会回落到 brk —— 这条残余风险写进 §10，不假装不存在。


## 9. 退出、信号、线程、栈

| 情形 | exec 路径 | JIT | 对外是否一致 |
|---|---|---|---|
| `main` 返回 | `_start` → `exit_group(rc)` | 桩 `ret` → 宿主 `return rc` | 一致（退出码相同） |
| 程序里 `exit(3)` / `abort()` | `exit_group` / 134 | 同样 `exit_group`，把**编译器进程**带走 | 一致（退出码相同；munmap/清理跳过，无所谓） |
| 被信号杀（`raise(15)`） | 子进程死，编译器报 128+15 | 编译器自己死，shell 报 143 | 一致（`run-flag.sh` 第 4 条照样过） |
| 程序 spawn 的线程 | 随进程退出被收 | **主线程返回后可能还活着** | v1 不 `munmap`：撤映射等于给它们发段错误；进程退出时内核统一回收 |
| 栈 | 内核给的 8 MB | 宿主 `main` 栈的剩余量 | 深递归的余量略小（编译器栈帧已经让出去了）；后续可选"独立栈"（§13 末尾两行） |

`-test` 的收尾 `__c6c_test_summary` 会 `join` 所有线程（`lib/rt.c6`），所以 `-test`
这条路不受上面第四条影响。

## 10. 与 exec 路径的行为差异（要写进 README 的表）

| 面 | exec 路径 | JIT | 说明 |
|---|---|---|---|
| 临时文件 | 1 个 | 0 个 | 本轮目标 |
| 可写 `TMPDIR` | 必需 | 不需要 | 本轮目标 |
| `execve` 次数 | 2 | 1 | 本轮目标 |
| `getpid()` | 子进程 | 编译器自己 | 程序里 `kill(getpid())` 打到编译器上（行为一致，见 §9） |
| `/proc/self/exe` | 临时 ELF | c6c | 读它的程序会看到 c6c；tcc `-run` 也一样 |
| 地址空间 | 干净 | 与编译器共享 | 程序的野指针可能踩到编译器静态区（编译已结束，影响限于退出路径） |
| 环境变量 / 文件描述符 / 工作目录 | 继承 | 继承 | 相同 |
| 堆 | 进程自己的 `brk` | 私有 mmap arena（64 MB） | 超过 64 MB 回落 `brk`，见 §8 |
| 栈 | 8 MB | 宿主 `main` 栈剩余 | 深递归余量略小 |
| 信号处理 | 进程自己的 | 与编译器同一进程 | 程序装的处理函数会影响编译器（只在 `-run` 这一次进程里） |
| 镜像大小上限 | 文件系统 | 预留窗（默认 64 MB，可调） | 超限明确报错 |

## 11. 代码改动清单

### 11.1 新增 `src/jit.c6`（约 200 行）

见 §3 的动作表 + §4/§5 的 mmap/mprotect/窗口布局 + `jit_sym`（按名字取符号地址，
和 `embed.c6` 的 `c6_get_symbol` 同一套查法；因为 include 顺序在 `embed.c6` 之前，
这里单独实现一份小的，注释里互相指一下）。

`src/c6c.c6` 里加一行 `#include "jit.c6"`，位置在 `parse.c6` 之后、`main.c6` 之前
（要用 `DATA_IMG`/`CODE_IMG`/`X64`/`PA_BASE`，并且 `main.c6` 要用 `jit_*`）。

### 11.2 `PA_BASE`：`ELF_BASE_VA` → 变量（8 处）

`src/elf.c6` 紧挨现有常量加：

```c
/* 镜像里所有绝对地址用的基址。`-o` 写文件时永远是 ELF_BASE_VA（0x400000）；
 * `-run`（JIT）在解析前把它换成 mmap 出来的窗首（见 src/jit.c6）。
 * 注意：elf.c6 写 ELF 头时**仍然**写死 ELF_BASE_VA，所以 -o 产物不受影响。 */
static u32 PA_BASE = ELF_BASE_VA;
```

替换点：

| 文件:行 | 现在 | 改为 |
|---|---|---|
| `src/x64.c6:503`（`x64_data_sym`） | `ELF_BASE_VA + img_off` | `PA_BASE + img_off` |
| `src/x64.c6:551`（`x64_emit_asmblob`） | 同上 | 同上 |
| `src/x64.c6:644`（`x64_func_begin`） | 同上 | 同上 |
| `src/asmx.c6:1360`（`-S` 的符号 va） | 同上 | 同上（默认值不变，产物字节不变） |
| `src/parse.c6:7153`（`DATA_VA` 宏） | `ELF_BASE_VA + ELF_HDR + off` | `PA_BASE + ELF_HDR + off` |
| `src/parse.c6:20918`（`x64_finish_label_addrs`） | `ELF_BASE_VA + code_off` | `PA_BASE + code_off` |
| `src/parse.c6:20920`（CLBL 回填） | `ELF_BASE_VA + code_off + …` | `PA_BASE + code_off + …` |
| `src/parse.c6:20931`（符号 va 重算） | `ELF_BASE_VA + off` | `PA_BASE + off` |

`elf.c6:166/185/186`（写 ELF 头）**不改**。

### 11.3 `x64_emit_start_jit`（`src/x64.c6`，约 40 行）

§6 的指令序列。签名带上 `int with_fs`（= `NTLS > 0`）。

### 11.4 `pa_finish` 的分叉（`src/parse.c6`）

```c
main_sym = (u32)PA_MAIN->val;
entry_off = CODE_IMG.n;
if (JIT_ON)
    x64_emit_start_jit(&X64, main_sym, CTORS, NCTOR, DTORS, NDTOR, NTLS > 0);
else
    x64_emit_start(&X64, main_sym, CTORS, NCTOR, DTORS, NDTOR);
/* ①..⑨ 全部不变（只把基址换成 PA_BASE） */
if (JIT_ON) {
    if (jit_fill(entry_off + code_off) < 0)      /* 拷进窗 + 拆权限，不再是 elf_write_parts */
        return 1;
} else {
    elf_write_parts(out, hdr, &DATA_IMG, &CODE_IMG, entry_off + code_off);
}
```

`if (!PA_QUIET_OUT) printf("c6c: %s（%u 字节…")` 在 JIT 模式下由驱动把
`PA_QUIET_OUT = 1`（和现在的临时文件路径一样，`-run` 成功时不出声）。

### 11.5 驱动（`src/main.c6`）

```c
int tmp_out = (O.mode == M_RUN) || (O.test && !O.out);   /* 保留：逃生口/对照 */
int jit = 0;
...
if (tmp_out) {
    if (jit_wanted()) {                 /* C6C_RUN != "exec" */
        if (jit_begin() < 0) return 2;  /* mmap 预留 + 设 PA_BASE */
        jit = 1;
        PA_QUIET_OUT = 1;
        out = "<jit>";                  /* 仅作占位，native 路径不会 fopen 它 */
    } else {
        if (run_make_tmp() < 0) return 2;
        out = RUN_PATH;
        PA_QUIET_OUT = 1;
    }
}
/* 编译（单文件 / 多 TU 两条分支都不变） */
if (jit) {
    if (rc == 0) {
        if (O.test) {
            /* 测试入口不收参数（和 tcc 的 -test 一样），argv[0] 用源文件名 */
            char *targv[2] = {0};
            targv[0] = (char *)O.files[0];
            rc = jit_run(1, targv);
        } else {
            /* 就是程序自己的 argv（argv[0] 是源文件名），和老的
             * run_elf(argv + O.run_argi) 同一个向量 */
            rc = jit_run(argc - O.run_argi, argv + O.run_argi);
        }
    } else {
        jit_abort();                    /* 编译失败：整块窗还回去 */
    }
} else if (tmp_out) {                   /* 老路径原样保留 */
    ...
}
```

`-run` 与 `-o`/`-c`/`-E`/`-fsyntax-only`/文本后端的互斥检查全部不动。

### 11.6 `lib/rt.c6` 两处小改

- `c6c_brk_cur` / `c6c_brk_left` 去掉 `static`（§8）。
- 新增 `__c6_fs_get` / `__c6_fs_set`（§7）。

### 11.7 不改

- `elf.c6` 的 ELF 写出、`obj.c6`、`ld.c6`、`c99.c6`、`llvm.c6`、`asmout.c6`、`aout.c6`。
- `-c` / `-S` / `-o` 的任何字节。
- `embed.c6`（本轮不动，见 M5）。

## 12. 逃生口与诊断

| 开关 | 作用 |
|---|---|
| `C6C_RUN=jit`（默认） | 走 JIT |
| `C6C_RUN=exec` | 退回"临时 ELF + exec"（旧路径，用于对照与排障） |
| `C6C_JIT_CAP=<MB>` | 改预留窗大小（测试超限报错用） |

错误消息（都要能一眼看出下一步）：

```
c6c: error: JIT 预留 64 MB 装不下（镜像需要 N 字节）：调大 C6C_JIT_CAP，或用 C6C_RUN=exec
c6c: error: JIT 装不进去（mmap 64 MB 失败）
c6c: error: JIT 改不了段权限（mprotect 失败）
c6c: error: JIT 基址 0x… 超出 4 GiB（X64Sym.va 是 u32）
c6c: error: 不认识的 C6C_RUN 值 'x'（支持 jit / exec）
```

`-run` 的用法行（`usage()`）与 README 的 `-run` 段要同步改成"进程内跑"。

## 13. 后续（本轮不做，写在这儿免得丢）

| # | 项 | 价值 |
|---|---|---|
| J-14 | `embed.c6` 的 `c6_run` 换成本轮的装载器 | 库形态不再依赖 `0x400000`（现在自举产物里必然失败） |
| J-15 | `X64Sym.va` → `u64`、`PA_BASE` → `u64` | 去掉 < 2 GiB 限制，可用普通 `mmap` |
| —（未列入 todo） | 入口桩跑在独立 `mmap` 栈上（复用 `__c6_swapstack` 的思路） | 程序拿到干净的 8 MB 栈，深递归与 exec 路径等价 |
| —（未列入 todo） | 未定义符号用宿主的动态符号表兜底（`dlsym`） | 只在"宿主自己带 libc"的库形态有意义；`rt.c6` 里没有 `dlopen`，先不做 |
