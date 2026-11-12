# 并发：async / await / co / task / spawn

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> 无栈协程（`co` 顶层跑、`task` 拿句柄、`await` 挂起）与 OS 线程（`spawn` 立刻返回、真并发）。
>
> 相关：示例 `examples/async_demo.c6`、`examples/thread_demo.c6`；用例 `tests/async.c6`、`tests/spawn.c6`

---

## async / await（无栈协程）

```c
async int add(int a, int b) { return a + b; }

async int worker(void)
{
    int got = 0;
    await connect();               /* 语句式 */
    await send();                  /* 语句式 */
    got = await recv();            /* 赋值式 */
    return got;
}

int main(void)
{
    struct worker_task *t = worker();   /* 调用只建任务，不执行函数体 */
    int rc = __c6_co_wait(t);           /* 驱动到完成，0 = 正常结束 */
    printf("%d\n", t->val);
    __c6_co_destroy(t);
    return rc;
}
```

`async` 和无栈协程的区别在于**没有自己的栈**：任务块一次分配出
`struct f_task` 加函数帧，函数体直接在这块内存上跑，`await` 只把错误槽
存进任务就 `return` 回驱动方；恢复时把错误槽放回去，再跳到那个恢复点。
帧不搬也不拷，所以局部变量和 `defer` 的注册状态（都在帧里）在挂起前后
完全不变，也不需要上下文切换。（把帧交给 body 需要后端配合：`c67` 目标做不到，
它上面 `async` 是编译期错误；六个有 frame-in-task 支持的目标——x86_64、i386、
arm、arm64、riscv64 与 C99 输出后端——都走"帧住在任务里"。）

速查：

| 写法 | 行为 |
|---|---|
| `async T f(…)` | 声明一个协程；调用 `f(…)` 得到 `struct f_task *` |
| `await f(…)` | 建子任务、推进它、等它完成后取 `val` |
| `await c6_io(fd, ev)` | 唯一的叶子挂起原语：等 `fd` 就绪，值是就绪掩码 |
| `t->val` / `t->pN` | 结果值 / 第 N 个实参（任务结构由声明推导） |
| `__c6_co_wait(t)` | 驱动到完成；`0` = 正常结束，`-1` = 子任务以 `err` 结束 |
| `__c6_co_destroy(t)` | 释放任务槽（含未完成的子任务） |
| `co f(…)` | 顶层协程：建完立刻跑到第一个必须等待的点，然后返回 |
| `task t = f(…)` | 建任务并拿句柄；作用域退出时自动 `__c6_co_destroy(t)` |
| `__c6_co_run()` | 把 `co` 出去的任务都跑完；返回其中以 `err` 结束的个数 |

规则：

- `async` 是声明说明符，和 `static` / `inline` 同级；只能定义在**文件
  作用域**，不能嵌套、不能 `async inline`、不能是 `main`；
- 签名必须是能从声明写回任务结构的类型：标量、枚举、指针、`T[:]`、
  文件作用域的 struct/union。数组、VLA、函数指针参数都会报错；
- 帧住进任务需要后端把帧寄存器指向任务内存；做不到的目标（`c67`）在编译期
  直接拒绝 `async`（`'async' is not supported on this target`）；
- 三种写法都可用：`await f();`、`a = await f();`、`await f(), a = await g();`
  也可以写在参数里（`f(a, await g())`）和条件里
  （`while (await c6_io(fd, C6_CO_EV_READ) > 0)`）；
- 挂起点上压在求值栈里的中间值必须落在**变量**里。帧槽会随帧一起恢复，
  寄存器不会——不过调用点本来就会把活寄存器落到帧槽，所以
  `x + await f()` 也能用；
- `await` 不能写进 `defer` / `errdefer` 体和 `sizeof` 里；
- `co f(a);` 和 `task t = f(a);` 的操作数都必须是一次 `async` 调用：
  `co` 建的是**顶层**任务（不归调用者、不做作用域回收，交给
  `__c6_co_run()`），`task` 建的任务归作用域管；
- `co` 是**同步起点**：任务在 `co` 语句处就开始跑，跑到第一个必须等待的
  点才把控制权交回来。`await c6_io` 总要停一轮（即使 fd 已经就绪，唤醒是
  下一轮 `epoll_wait()` 的事）；`await` 子任务不算等待，所以不碰 fd 的
  `co f();` 会在 `co` 语句里跑完并当场回收槽位；
- `co`、`spawn`、`task` 是**软关键字**：只有在语句开头、后面紧跟一个标识符
  时才是关键字。`int spawn = 1;`、`spawn = 2;`、`x = spawn;`、`spawn(x);`、
  `typedef int co; co v;`、`task` 做变量名和 `spawn:` 标签都照常工作；
  起线程的关键字以前写作 `go`（和 Go 的 goroutine 同名，容易误解），
  现在 `go` 退回普通标识符，旧写法 `go f(a);` 报
  `'go' was renamed to 'spawn'`；
- `c6_io` 只有 `await c6_io(fd, ev)` 一种写法；`ev` 是
  `C6_CO_EV_READ` / `C6_CO_EV_WRITE`；
- `c6_io` 的 `fd` 只有一条限制：不能是负数（负 fd 的任务按失败回收，
  `__c6_co_wait` 返回 `-1`，`__c6_co_run` 计一次失败）。挂起用的是 `epoll(7)`（没有 epoll 的平台退回 `poll(2)`），
  不是 `select(2)`，所以**没有 `FD_SETSIZE` 那种 1024 的描述符窗口**：
  只要进程能打开这个 fd，就能挂上去；
- 框架的开销与代价、以及和 C / C++ / Go / Rust 的实测对比，见
  [async-comparison.md](../async-comparison.md)；
- 每个任务一块内存：`struct NAME_task` + 原生帧（帧里带着自己的清理状态），按需
  `malloc`（没有固定池、没有 4 KiB 帧上限、**没有任务数上限**：同时存活多少个
  协程只取决于内存）。后端把帧寄存器直接指向这块内存，所以挂起**不拷贝**任何
  东西，`&local` 跨过 `await` 仍然有效；`weak` 局部/形参/返回值因此在 `async`
  里也合法。`__c6_co_wait` 和 `__c6_co_run` 都用 `epoll` 等 fd（一次 `epoll_wait`
  等全部 park 住的任务，事件数组按需增长），`__c6_co_run` 对顶层任务做轮转。
  协程本身是协作式、单线程的；要真并发用 `spawn`。`-b`（边界检查）不能与
  `async` 同用。

完整可运行示例见 [examples/async_demo.c6](../../examples/async_demo.c6)
（一次 HTTP 请求，传输层用 `pipe()` 代替 socket，离线可跑）：

```sh
./tcc/c6 -B tcc -run examples/async_demo.c6
```


## spawn（OS 线程）

> 旧名 `go`：这个关键字以前拼作 `go`，因为和 Go 的 goroutine 同名（这里是
> 真 OS 线程）改成了 `spawn`。`go` 仍是普通标识符，`go f(a);` 这种旧形状报
> `'go' was renamed to 'spawn': write 'spawn f(...)'`。

```c
int download(int id)               /* 普通函数，阻塞也没关系 */
{
    defer printf("worker %d 结束\n", id);
    printf("worker %d 开始\n", id);
    sleep(1);
    return id;
}

int main(void)
{
    spawn download(1);              /* pthread_create，立刻往下走 */
    spawn download(2);
    printf("main 继续跑\n");       /* 上面两行可能还没打印 */
    return 0;                      /* 退出前编译器插入 __c6_th_join_all() */
}
```

`spawn f(a);` 是一条**语句**，不产生值：

| 写法 | 对函数的要求 | 引擎 | 返回 | 真并发 | 句柄 |
|---|---|---|---|---|---|
| `co f(a);` | 必须是 `async` | 无栈协程（帧住在任务里） | 跑到第一个真正要等的地方 | 否 | 无（交给顶层队列） |
| `task t = f(a);` | 初始化式是 `async` 调用 | 同上 | 同上 | 否 | `t->wait()` |
| `spawn f(a);` | 任意普通函数 | `pthread_create` | **立刻** | **是** | 无 |
| `thread h = spawn f(a);` | 同上 | 同上 | 立刻 | 是 | `h->join()` |

句柄的方法是同一套写法，语义按引擎各算各的：

```c
task t = fetch(url);      t->wait();   /* 驱动事件循环直到它结束（顺带推进别的任务） */
thread h = spawn download(1); h->join();  /* 只等这一个 OS 线程，别的都不动 */

if (t->done()) ...        /* 结束了没：不驱动、不阻塞 */
if (t->kind() == C6_CO_KIND_ERR) ...   /* 失败的任务：wait() 也返回 -1 */
int n = (int)h->value();  /* 线程函数的返回值（整数或指针） */
```

名字不同是有意的：`t->wait()` 会推进整个事件循环，`h->join()` 是纯阻塞。

规则：

- 参数在 `spawn` 语句处**求值一次**，按值复制到堆上的参数块，线程解开参数块
  再调用 `f`；之后改局部变量不影响线程。支持标量、枚举、指针、`T[:]`、
  文件作用域的 struct/union；
- 不能 `spawn` 一个 `async` 函数（用 `co`，错误信息会提示）、不能 `spawn`
  变参函数、不能 `spawn` 带 `!error` 的函数（错误没人接）、不能通过函数指针
  `spawn`；
- 被调函数只要在调用点有个**文件作用域的声明**就行，定义可以在任何一个
  翻译单元；跳板按被调函数生成，每个 TU 各生成一份（`static`），没有被
  `spawn` 用到的原型不产生任何代码；
- 线程由**启动它的线程**回收：`main` 结束时编译器插入
  `__c6_th_join_all()`，别的线程在退出时回收自己的子线程，所以
  `-run` 也不会在线程还在跑的时候释放 JIT 内存。需要中途等待时可以自己
  调用 `__c6_th_join_all();`；
- 需要等某一个线程就用句柄：`thread h = spawn f(a); h->join();`（重复
  `join()` 幂等，`h->done()` 不阻塞，`h->value()` 取被调函数的返回值，
  整数或指针）。句柄是**启动者私有**的，不要跨线程传；作用域退出时自动
  归还，没 `join` 也不漏 —— 线程本身仍旧由启动者回收；
- 每个线程有自己的错误槽、插值构造器和协程池（`pthread_key`，不是
  `__thread`，`-run` 不支持 TLS）。`defer` 不需要它们：清理状态在函数帧里，
  线程里、ISR 里照常写；`err` / `f".."` / `async` 才要那块每线程状态，
  线程里也一样能写，还能再 `spawn`；
- 运行时只用 `pthread_create` / `pthread_join` / `pthread_key_*`；
  glibc 2.34 起这些都在 libc 里，老系统需要 `-lpthread`；
- 这些符号是 **weak** 的：链接里没有 pthread 时（无 libc 的 freestanding
  程序）运行时退化成单线程，`spawn` / `thread` 报告启动失败，而不是链接报错；
- PE 目标还没有线程支持，`spawn` 在那边会触发 trap。

完整可运行示例见 [examples/thread_demo.c6](../../examples/thread_demo.c6)：

```sh
./tcc/c6 -B tcc -run examples/thread_demo.c6
```
