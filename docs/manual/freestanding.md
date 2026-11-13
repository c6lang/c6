# 无 libc（freestanding）与裸机

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> 扩展不依赖 libc 也能跑：运行库的外部依赖全是 weak，缺 `malloc` / `pthread_*` / `select` 时自己顶上，一直到 512 字节引导扇区进长模式。
>
> 相关：示例 `examples/freestanding_*.c6`、`examples/boot/`；目标 `cmake --build build --target fstest*` / `cmake --build build --target fstest-bare*`

---

## 无 libc（freestanding）

`libtcc1` 里的运行时（插值构造器、`err`/`catch`、托管引用、协程池）
本来要 `calloc` / `free` / `realloc` / `memset` / `memcpy` / `memmove` /
`pthread_*` / `select`。这些引用现在全是 **weak** 的，所以没有 libc 也能链接：

| 需要的东西 | 没有 libc 时 |
|---|---|
| `calloc` / `free` / `realloc` | 用 `libtcc1` 里的静态 arena（`.bss`，默认 256 KB，编译 libtcc1 时 `-DC6_HEAP_SIZE=...` 可调）；程序自己定义 `calloc` 就用程序的那份 |
| `memset` / `memcpy` / `memmove` | `libtcc1` 自带字节循环版，真 libc 的强定义会在链接时覆盖它们 |
| `pthread_*` | 退化成单线程（一个静态的状态块）；`spawn` / `thread` 报告启动失败 |
| `select` | 只有 `async` / `c6_io` 用到；没有就报“等待失败”，其它照常 |

也就是单线程的 freestanding 程序直接可用，程序这边什么都不用提供：

```sh
./tcc/c6 -B tcc -static -nostdlib -o build/freestanding_fstr \
    examples/freestanding_fstr.c6 tcc/libtcc1.a   # 或 cmake --build build --target fstest
./build/freestanding_fstr
```

[examples/freestanding_fstr.c6](../../examples/freestanding_fstr.c6) 没有 `main`、没有
crt，入口是 `_start`，输出和退出都自己发 syscall，里面跑了整数/浮点插值、
格式说明、`@f".."`、`defer`、`err`+`catch` 和 `@T`，8 条断言全过。

要 `spawn` / `thread` 就再实现一层线程接口 —— 运行时只用这 8 个入口，都是
weak 的，程序给了就切回“每线程一块状态”：

| 入口 | 干什么 |
|---|---|
| `pthread_once` / `pthread_key_create` / `pthread_getspecific` / `pthread_setspecific` | 每线程一块状态（错误槽、插值构造器、协程池）。只有用到 `err` / `try` / `catch`、`f".."` 或协程的函数才去取它，纯 `defer` 的代码不碰。`key_create` 的析构函数务必在线程退出时调用，否则每个线程漏约 100 KB |
| `pthread_create` / `pthread_join` | `spawn` 的启动与回收 |
| `pthread_self` / `pthread_equal` | `thread h = spawn f(...)` 用它判断“自己 join 自己” |

三个坑：

- **这一层必须用普通 C 或汇编编译（`.c` / `.plain.c`），不能写成 `.c6`**：用了 `err` /
  `try` / `catch`、`f".."` 或协程的 c6 函数会去取 `tcc_th()`，而 `tcc_th()`
  正要去调 `pthread_once` / `pthread_key_create` —— 用 c6 写就是 bootstrap 里
  递归自己，栈一冲就段错误。纯 `defer` 的函数现在不碰 `tcc_th()`（状态在
  帧里），但这一层还是照旧用普通 C 写更稳妥；
- 一旦真的多线程，`calloc` / `free` 必须是线程安全的：`libtcc1` 里那个静态
  arena 只在单线程下安全，这时请由程序提供 `calloc` / `free`；
- 线程里用 `async` / `c6_io` 还需要 `select`。

例子：[examples/freestanding_pthread.c](../../examples/freestanding_pthread.c) 就是一份
可以直接抄的实现 —— x86-64 Linux 上 `clone` + `futex`，不碰 libc：线程指针放
`%fs:0`（`arch_prctl`），栈和控制块用 `mmap`，线程退出用 `SYS_exit`，join 用
`CLONE_CHILD_CLEARTID` 加 futex；它还顺带给了一份线程安全的 `calloc`/`free`
（每次 `mmap`，不用锁）。[examples/freestanding_go.c6](../../examples/freestanding_go.c6)
用它跑两个真线程（一个等另一个置位，只有真并发才会通过），`cmake --build build --target fstest-go`
编译并运行，200 次连跑没有失败。

join 这里有个坑值得记下来：「还活着」的标志不能直接靠 `CLONE_CHILD_SETTID` 写
的那个 tid —— 它是子线程**开始跑**的时候才写的，父线程可能先看到 0，于是 join
提前返回、把子线程马上要用的栈 `munmap` 掉，子线程一上来就段错误。这份实现的做
法是父线程在 clone 之前先把该字写成 -1，只让内核在子线程真正退出时清零。

`-static` 不是必须的，但少了它 ELF 里会留一个 `PT_INTERP`（内核会先跑
`ld.so`）；加上之后这两个产物既没有 `DT_NEEDED` 也没有 `PT_INTERP`，是一个
纯粹的静态 ELF —— 换到别处跑时只要有人能加载它就行。

#### 连操作系统都没有的时候

`freestanding` 在 C 标准里指的是「不要求有 C 库」——启动、结束、输出由实现
自己定；它**允许**没有操作系统，但不等于必须裸机。上面两个例子就还是站在内核
的 syscall 上（`write` / `exit` / `mmap` / `clone` / `futex`）。

真裸机上要提供的只有「程序自己想做的事」，运行时这边可以一个外部符号都没有
（`nm -u libtcc1.o` 里只剩 weak 的 `calloc` / `free` / `realloc` / `select` /
`pthread_*`）：

| 平时由谁提供 | 裸机上 |
|---|---|
| `calloc` / `free` / `realloc` | 不给也行：`libtcc1` 里的静态 arena 顶上（`.bss`，`-DC6_HEAP_SIZE=` 可调）；要自己的分配器就写一个 bump |
| `memcpy` / `memset` / `memmove` | `libtcc1` 自带的 weak 版本，什么都不用做 |
| `pthread_*` | 不给也行：单线程、一个静态状态块，`spawn` / `thread` 报启动失败；要并发就把板子上的线程 API 包成那 8 个名字 |
| `select` | 只有 `async` / `c6_io` 用；不用 async 就永远碰不到 |
| 输出、退出、复位 | 程序自己的事：把 `write` 换成 UART/MMIO 写、`exit` 换成 `while (1)`，入口符号交给链接脚本和向量表 |

板上常做的四件事在 `std/` 里有现成的零依赖实现：`std.core.crc`（CRC-8/16/32C
与 Fletcher/LRC/XOR/累加和）、`std.core.fixed`（Q16.16 与 Q15 定点、带 sin/cos）、
`std.core.pool`（借用一段 `.bss` 的定长块分配器）、`std.encoding.frame`（COBS /
SLIP 串行分帧）。它们只依赖 `std.core.types`，不分配、不链 libc/libm、不碰
`pthread`，把 `.c6` 正文列进裸机 payload 的命令行即可（用法见
[stdlib.md](../stdlib.md#嵌入式基础crc--fixed--pool--frame)）。

[examples/freestanding_noos.c6](../../examples/freestanding_noos.c6)（`cmake --build build --target fstest-noos`）
就是这么一份：里面**一个 syscall 都没有**，插值、格式说明、`defer`、`err` +
`catch`、`@T` 全在内存里做完，最后用 `ud2`（全过，退出码 132）或往 0 地址写
（某个断言失败）报信 —— 这两个「报信」在裸机上就是你的异常向量。

注意：编译器自己还是得在别的地方跑（宿主上的 `tcc` 产出静态 ELF，再交给引导
程序/烧写工具）；`tcc -run` 那套 JIT 与裸机无关。

`cmake --build build --target fstest-qemu` 会把这三个产物再用 `qemu-x86_64`（qemu 的用户态模拟器）跑
一遍：它不用本机的加载器，也不需要 libc —— syscall 由 qemu 自己实现，等于换了
一套内核接口实现来验证同一份二进制。这仍然不是裸机（qemu 在里面扮演内核）。

#### 真裸机：自己写的 512 字节引导器

`cmake --build build --target fstest-bare` 走的才是没有内核那条路：qemu 只提供一台**空机器**，
BIOS 把 512 字节引导扇区读到 `0x7C00` 就撒手，剩下全是自己的代码。

```
BIOS → 引导扇区（examples/boot/boot.S，512 字节）
     → 实模式 INT 13h 分块把 payload 读到 0x10000
     → 开 A20、装 GDT、进 32 位保护模式
     → 0x90000 建页表（前 1GB 恒等映射）、开 PAE/LME/分页、切 64 位
     → payload 拷到 0x400000、清零 .bss
     → call _start（c6 程序）
```

结果（调试口 `0xE9` 是这台机器上唯一的输出设备）：

```
$ cmake --build build --target fstest-bare
n=42 hex=002a pi=3.142
s=bare p=42 managed=<42>
bare metal: ok
fstest-bare: ok（裸机启动，全部断言通过；KVM）
```

`examples/boot/main.c6` 里除了插值、格式说明、`defer`、`err`+`catch`、`@T`、`@f".."`
这些语言特性，还顺手验证了运行时：分配器用 `libtcc1` 自带的静态 arena，`mem*`
用 weak 版本，**没有一个外部符号**（`pthread` / `select` / `calloc` 全都不给）。
「输出」是两条 `outb`（0xE9 调试口、0x501 退出），写在 payload 自己的全局
`__asm__` 里 —— 换块板子只要改这两条指令。

协程在这里一样能用：`co` / `async` / `await` / `task` / `__c6_co_run()` 的状态全在
内存里，调度器就是 `libtcc1` 里的 `tcc_co_pump()`，**一个 syscall 都不发**。
`examples/boot/main.c6` 里跑了两个交错的顶层 `co`（各自循环里反复 `await`、结果写
回全局），外加一条失败任务的计数。唯一需要外部实现的是 `await c6_io(...)`：它靠
`select` 等 fd，裸机上没有，`__c6_co_run()` 就按设计返回 -1（这个也断言了）；
`spawn` / `thread` 要的是另一层，见上面那张 pthread 入口表。

几个只有真裸机才会碰到的坑：

- **SSE 要自己开**。c6 的浮点/结构拷贝会用 `xmm`，长模式下 `CR4.OSFXSR` 不开
  就是 `#UD`（引导器里那三行 `CR0` / `CR4` 就是干这个的，漏了会在 payload 第一
  条 `movq %xmm0` 上炸）；
- **`objcopy -O binary` 不能直接用来导出 payload**。它按**节**的最低地址当基准，
  而 tcc 链接出来的 ELF 头 0x498 字节正占着 `0x400000..0x400498`，导出的镜像会
  整体错位 0x498 字节（症状：跳进去第一件事就是 `#UD`）。`examples/boot/flat.py`
  按程序头（`PT_LOAD`）摊平，和真装载器一个语义；
- **引导扇区必须正好 512 字节**，末尾 `0x55AA`；分块读每次 ≤ 16 个扇区，免得撞
  64KB 段边界；
- 引导扇区那几十行 16 位/32 位汇编用 binutils 的 `as` 编（tcc 的汇编器不认
  `.code16` / `.code32`），这让这个例子比别处多一个构建依赖 —— 语言特性本身
  不依赖它。

#### 把 tests/ 也搬上裸机

`cmake --build build --target fstest-bare-tests` 把 `tests/*.c6` 里的 test 块搬到裸机上跑：**35 个测试
文件、全部 test 块，0 失败**，每个文件一个镜像，一共十几秒。

```
ok   async: 20 test(s), 0 failure(s)
ok   enum_scope: 8 test(s), 0 failure(s)
ok   fstr: 15 test(s), 0 failure(s)
ok   generic_fn: 8 test(s), 0 failure(s)（块级跳过 1）
ok   try: 18 test(s), 0 failure(s)
...
bare tests: 35 通过, 0 失败（跳过: defer_thread gc_drop gc_thread method thread thread_handle tuple_thread）（块级跳过: generic_fn:1）
```

要补的不是运行时，而是**测试自己用的 libc**：
[examples/boot/shim.plain.c](../../examples/boot/shim.plain.c) 里是一层替身 ——
`printf`（整数/字符串，没有浮点）、`strcmp`/`strncmp`/`strlen`/`strstr`/
`memcmp`、`qsort`（插入排序）、`getpid`/`usleep`/`stat` 的桩子、**内存里的
管道和 `select`**（`await c6_io` 就靠它），外加一个 4MB 静态堆的首次适配
分配器。这层替身不是运行时要求的（`libtcc1` 的依赖全是 weak），是测试文件本身
按 POSIX 写的。

跳过的 7 个是真的缺东西，不是能补的：

| 测试 | 缺什么 |
|---|---|
| `thread` / `thread_handle` / `gc_thread` / `gc_drop` / `tuple_thread` | `spawn` 本身现在能跑了（见上面那节），但这几个用的是 `h->join()` / `h->value()`，那条路要走 `libtcc1` 的每线程块（`calloc`/`free` + pthread 键），裸机上还不稳 —— 任务收尾之后会三重故障，没修完 |
| `defer_thread` | 要 `clone`/`futex` 那套真正的线程创建 |
| `method` | 要 `FILE` 的真实内存布局（`stdout->fflush()`），那是 libc 的内部结构 |

整个文件都跳太重时，可以只跳一个 test 块：在块上一行写 `// BARE-SKIP`，
[examples/boot/harness.py](../../examples/boot/harness.py) 生成裸机那张表时会把它
略过（块号不变，同一个文件里剩下的块照跑）。`tests/generic_fn.c6` 的
「实例是普通函数：可以起线程」就是这么处理的 —— 它只缺线程，别的 8 个块
在裸机上照样有意义。

条件编译里没开的块不用标：harness 认 `#if` / `#ifdef` 的条件（裸机编译不带任何
`-D`，所以 `#if defined X` 一律当假），这种块既不进表也不占块号。
`tests/test-block.c6` 里藏在 `#if defined FAILING_BLOCK` 里的那一块就靠这个 ——
不然表里会多一个 `__tcc_test_7`，链接期报 unresolved reference。

三个只有真跑一遍才会知道的坑：

- **默认 256KB 的 arena 跑不了一整套**。裸机上没有 `calloc` 时运行时用
  `libtcc1` 自带的 bump 分配器，只增不减，跑到 `async` 那 200 个 task 的循环
  就没了。给上 `calloc`/`free`/`realloc`（替身里那个首次适配）立刻就好 ——
  这本来就是 README 里写的那两条路之一；
- **`select` 得自己实现**，而且 fd 0/1/2 要当"永远就绪"（真的 fd 1 也是），
  否则 `await c6_io(1, WRITE)` 会一直返回"不可写"；
- **一行命令里塞多个 TU 会撞上编译器一个老 bug**：带 `new`/GC 的 TU 和非首
  TU 一起编时，`c6_gc_new_lead` 会对 NULL 标签名做 `strcmp` 然后崩。拆成
  `-c` + 链接就绕开了（`build.sh` 现在就是这么做的），bug 本身没修。

C99 输出后端不在这个范围：它生成的 C 文件依赖宿主 libc（`<stdlib.h>`、
`<pthread.h>`）。

#### 裸机上的 spawn 是真并发：自己写一个抢占式调度器

`spawn` 在别的地方落到 `pthread_create` 上。裸机上没有内核，所以
`cmake --build build --target fstest-bare-go` 干脆自己写那一层：
[examples/boot/sched.plain.c](../../examples/boot/sched.plain.c) 就是最小内核 ——
8259A 重映射到 `0x20`、8254 每 10ms 一次时钟、中断进来换到调度器自己的栈、
把现场摆成一块 160 字节的 `struct Frame`，换任务就是换块 Frame。新任务的
帧是**编出来的**：寄存器全 0，加上 `rip/cs/rflags/rsp/ss` 五件套，第一次被
调度时 `iretq` 直接落进 `task_entry`，跑的是它自己那块 64KB 栈。

对 `libtcc1` 来说只是那 8 个符号（`pthread_create/join/once/key_create/
getspecific/setspecific/self/equal`），别的都不问。

怎么证明"真并发"？[examples/boot/go_bare.c6](../../examples/boot/go_bare.c6) 里两个
任务**都没有让出语句**，而且互相等：空转的那个先置 `started`，然后死等
`go_flag`；另一个先等 `started`，再把 `go_flag` 立起来。

```
spawn: 空转 4446664 次（上限 200000000）、时钟切换 5 次、go_flag=1
spawn: 是并发 —— 两个任务在同一个 CPU 上被时钟抢占着交错推进
```

#### 多核：两个核同时在跑

`cmake --build build --target fstest-bare-smp` 起两个核（`-smp 2`）跑
[examples/boot/smp_bare.c6](../../examples/boot/smp_bare.c6)，再用 `SMP=1` 跑一遍做
负对照。AP 是 `examples/boot/ap.S` 那个 16 位实模式跳板拉起来的（`as --32`
+ `ld -Ttext 0x8000`，222 字节，`build.sh` 用 `objcopy -I binary` 链进
payload），引导核先往 `0x7000/0x7008/0x7010` 写好 cr3、入口、每个 APIC ID 的
栈顶，再对每个 AP 走 INIT → SIPI 0x08 → SIPI 0x08。每个任务"自己在哪个核"
是从 LAPIC 的 ID 寄存器读的，不是猜的。

证据是这么来的：A 把**本核中断关掉**再空转两百万圈 —— 这段时间里引导核绝无
可能切到别的任务，可 B 的计数器还在涨，那就只可能是另一个核在跑它。

```
smp: 在线核数 2，A 跑在核 0
smp: B 跑在核 1（它自己从 LAPIC 读的）
smp: A 关中断空转期间 B 的计数器涨了 1219694
smp: 真并行 —— 两个任务同时跑在两个核上
```

`SMP=1` 时同一个程序打印"B 在核 0、涨了 0、只有交错没有并行"。分工上，引导
核继续用 8254 + 8259A 的时钟抢占调度（和单核那条路一模一样），AP 不装 IDT、
不开中断，只是从带自旋锁的任务表里捡 `T_READY` 的活干；状态机保证同一个任务
不会被两个核同时选中。两个硬伤级的坑：启动 AP 的整段必须 `cli`（时钟插进
`delay_10ms` 那种短函数里，`iretq` 恢复现场偶尔会落进栈地址），AP 上也绝不
能 `sti`（它没装 IDT）。细节在 [docs/design/plan.md](../design/plan.md)。

空转次数**远小于上限**就是证据：只有时钟中断真在任意指令边界把 CPU 抢走，
另一个任务才有机会跑起来把 `go_flag` 立起来。要是 `spawn` 只是在同一个栈上顺序
调用，先跑的那个得把 2 亿次整个跑完，另一个才轮得到。

单核上是真并发（交错推进），不是并行。要并行得多核，而多核要 AP 跳板
（16 位实模式代码，`tcc` 不支持 `.code16`，得另走 binutils），这部分还没做。

`spawn` 的**句柄**（`h->join()` / `h->value()`）在裸机上还没打通：句柄那条路
要 `libtcc1` 的每线程块走 `calloc`/`free` 和 pthread 键，实测任务收尾之后会
三重故障（`thread`/`thread_handle` 两个测试还是跳过）。所以 `go_bare.c6` 用
全局变量握手，不用句柄 —— 调度这一层本身是好的，`pthread_join` 用普通的
函数体任务测过（`pthread_create` + `join` + 返回值都对）。

两个只有真跑一遍才会知道的坑，写在文件头了：

- **长模式下的 `iretq` 总是弹 `rip/cs/rflags/rsp/ss` 五个 qword**，不是只在
  特权级变化时才弹。中断进来时 CPU 压了几个得看机器：这台（KVM）压五个，
  被打断的 `rsp` 就记在 `[入口 rsp + 24]` 那一格里 —— 直接读它，别自己算
  "入口 rsp + 24"，算出来偏低 16 字节（`ret` 弹到栈上就是 `#UD at
  0xffff..`，也就是 `fstest-bare-smp` 之前那个偶发）；
- **`boot.S` 里 `0x18` 才是 64 位代码段**（`0x08` 是 32 位的）。中断门的
  selector 写成 `0x08`，第一次时钟中断就 `#GP(0x08)`，而且看起来像"什么都没
  发生"（三重故障、qemu 退出码 0）；
- 顺带修了个潜伏的：引导器原来把栈放在 `0x80000`，那个地址其实**落在 payload
  的 `.bss` 里面**，变量一多（这次是中断栈和堆）就会踩到栈 —— 现在挪到 16MB。
