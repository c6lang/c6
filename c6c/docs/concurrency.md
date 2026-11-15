# c6c 的并发（thread / async / await）现状与计划

剩 7 个 tests/ 失败用例全是这条线：`async async_frame gc_async gc_thread
thread thread_handle`。现在 c6c 的口径是"**桩**"：

* `thread t = thread(f);` —— 立刻在当前线程同步调用 `f`，`t.wait()` 返回它
  的结果（`__c6_co_*` 一组桩，见 lib/rt.c6）。
* `async T f()` 合成 `struct f_task`，句柄 `t` 的 `val/wait()/done()/kind()`
  都到位了，但**任务体在创建时就一口气跑完**，没有"跑到第一个 `await` 就
  挂起"这回事。

所以断言"立刻跑到第一个 await 点""多个 co 轮转推进（真交替）""spawn 立刻
返回"的用例必然失败。这不是补几个函数能解决的，需要真的并发设施。

## 1. 卡在哪儿：没有 asm，也没有 libc 线程

* c6c 不支持 `asm`（tests2/99_fastcall 就是因此挂的），所以**拿不到
  "换栈"这条指令**（`mov %rsp` / `call` 到新栈）。协程的本质就是换栈。
* c6c 出的 ELF 是单 PT_LOAD、静态、不链 libc，所以 `pthread_create` 也不在。

## 2. 两条可行路线

### A. `clone()` + 线程模拟协程（不改语言，靠运行时）

x86-64 的 `clone` 系统调用可以直接给子线程一个栈指针：

```c
long tid = __builtin_syscall(56 /* clone */, flags, stack_top, 0, 0, 0);
if (tid == 0) { f(arg); c6c_exit(0); }
```

子线程从 `clone` 返回后立刻在**新栈**上调用 `f`，而父线程的栈没被动过，所以
"父继续跑、子在新栈上跑"是成立的（子线程不能从 `clone` 的返回点返回）。
这样能拿到：

* `thread`：真并发（`wait()` = futex 或自旋等 done 标志）。
* `async`：每个任务一条线程 + 一对信号量做**交接**（任何时刻只有一个在跑）。
  于是"跑到第一个 await 就挂起"= 任务线程在 `await` 上阻塞、把控制权交回
  创建者 —— 语义对得上，虽然底下用的不是同一根栈。

风险：`clone` 在 C 层用要在返回后立刻换函数，编译器在返回点上读栈上内容的
可能得避开（把 `f(arg)` 放进一个 `noreturn` 的小函数）；另外 `CLONE_VM`
共享地址空间后，`rt.c6` 里的分配器（arena/数据段）都必须是可重入的——现在
都是单线程假设，需要一把全局锁。

### B. 给 c6c 加最小 asm（`__builtin_swapstack`）

不做通用 asm，只在编译器里加一个内建：

```
__builtin_swapstack(void **save_sp, void *new_sp) -> void
```

后端直接发 `push`一串寄存器 / `mov [save], rsp` / `mov rsp, new` / `pop` 一串 /
`ret`。有了它，协程（`async/await`）可以在**一根线程**上跑，语义最接近上游；
`thread` 仍然要路线 A 的 `clone`。

## 3. 建议顺序

1. **T1**：`rt.c6` 加 `__c6_clone(f, arg)`（`clone` + 新栈 + `noreturn` 包装），
   把 `thread` 换成真线程，`wait()` 用 futex 等 done 标志。
   验收：`tests/thread.c6`、`tests/thread_handle.c6`、`tests/gc_thread.c6`。
2. **T2**：运行时加一把全局锁（arena/数据段/引用计数都过它），保证 T1 之后
   多线程下不崩。
3. **T3**：`async/await` 改成"任务 = 线程 + 信号量交接"，`wait()` 语义不变。
   验收：`tests/async.c6`、`tests/async_frame.c6`、`tests/gc_async.c6`。
4. **T4**（可选，长远）：走路线 B，把协程做成真挂起，省掉每条任务一根线程。

在这之前，`async`/`thread` 的桩口径要写进 README 的"已知限制"，免得看起来
像"已经支持了"。

## 4. 已经试过的：clone 直接起线程（失败，卡在帧指针上）

按路线 A 在 `lib/rt.c6` 里写过一版真线程（`pthread_create` = `clone(CLONE_VM|
CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM, 新 mmap 栈)`，
子线程那一支只读全局槽位、判据是 `gettid() != TH_SELF`，`pthread_join`/`__c6_th_join_all`
用 futex 等 `done`），`tests/thread.c6` 直接 SIGSEGV：

```
0x42baab: mov %rax,-0xa30(%rbp)     rbp = 0, rsp = 主栈
```

根因很清楚：**子线程是从 `syscall` 返回进来的，不是从 `call` 进来的**，c6c 的
函数体里 `rbp` 当帧基用（`-0xa30(%rbp)` 这种）。父线程的 `rbp` 指在父栈上、
而 `rsp` 已经换到新栈 —— 两个不一致，回调里任何一次 `rbp` 相对寻址都会踩错。
把栈指针对齐、让出两格补 `rsp % 16 == 8` 都没用，问题不在对齐。

结论：**没有一小段汇编就没法安全地"从新栈开始执行 C 代码"**。glibc 的
`clone` 包装就是一小段 asm（把 `fn`/`arg` 放进寄存器、`xor %rbp,%rbp` 后
`call`）。所以路线 A 也必须先有 B 的那个内建（或一个 12 条指令的汇编跳板），
这条已经写回工具链的 asm 缺口上：`asm` 支持是并发的前置条件，不是可选项。

## 5. 线程做完了，剩下的是协程（3 个用例）

第 173~175 轮把线程那半边做完了：`__c6_th_spawn` 跳板（clone）、真
`pthread_create`/`join`、句柄的 `done()/value()`、原子引用计数、spawn 实参块持有
一份引用。`tests/thread.c6`、`tests/thread_handle.c6`、`tests/gc_thread.c6` 都过了。

剩下 `async`、`async_frame`、`gc_async` 三个用例要的是**协程能挂起**。已经定了
方案（不追求文档里"无栈"的写法，先要语义对；栈ful 才好实现，语义上兼容）：

### 5.1 实现：一根线程 + 交接计数当协程

每个任务一根线程（栈 256 KB，和 spawn 同一套 `clone` 跳板），但**任何时刻只有
一个在跑**：谁跑由一对计数器决定。

```
C6Co { long in, out; void *task; void *(*fn)(void*); void *arg; long done; int state; void *blocked_on; }
```

* `co_resume(c)`（调度侧）：`c->in++`、futex 唤醒 `in`，然后阻塞等 `c->out` 涨一格；
* `co_yield(c)`（协程侧）：`c->out++`、唤醒 `out`，然后等 `c->in` 涨一格。

于是"挂起"= 把控制权交回调度侧，"恢复"= 交给它。`await` 的语义就是：
* 目标已结束 → 直接读值；
* 没结束 → 当前协程标 `blocked_on = 目标`、`co_yield`；调度侧把目标推进一格，
  目标再挂起时把等待者重新放回可运行队列。

调度侧（`__c6_co_run`）：可运行队列 + 全部协程链表，`while (还有没结束的)` 挑一个
可运行的 `co_resume` 一格。

### 5.2 编译器要改的四处

1. **任务体**：`task_new()` 之后立刻 `__c6_co_publish(task)`——任务指针要能被
   调度侧看到（函数体可能 `await` 挂起，调用方那时还没拿到返回值）。
2. **async 调用点**（`gen_call` 里）：当被调函数的返回类型是任务结构指针时，不发
   普通 call，而是照 spawn 的样子搭实参块 `{thunk, target, args…}`（复用
   `spawn_thunk(n, mgdmask)`），然后
   `co = __c6_co_new(env)` → `__c6_co_resume(co)` → 表达式的值 =
   `__c6_co_task(co)`。第一次 resume 只会跑到第一个 `await` 点，这就是
   "co 立刻跑到第一个 await 点"。
3. **`await t`**：在取 `t->val` 之前发 `__c6_co_await(t)`。
4. **`co f(args);`**：调用点那套 + 跑完自动 `__c6_co_destroy`。

运行时已有的 `__c6_co_wait/state/kind/destroy` 保持签名（都拿任务指针当身份），
内部改成"先驱动到结束再返回"，并通过任务指针反查 C6Co。
