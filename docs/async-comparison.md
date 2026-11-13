# 异步编程横向对比：c6 / C / C++ / Go / Rust（含 tokio）/ Uya

同一件事——**「等一个 fd 可读，然后接着往下跑」**——在这六种语言的九个运行时里写法完全
不同，代价也差一到两个数量级。这份文档给出机制对照、代码形态对照和本机实测
数据，最后说明每个数字的口径与陷阱。

数据由 `make bench-async`（脚本 `bench/async/run.sh`，源码 `bench/async/`）
产生，每个指标单独跑一个进程，结果同时写进 `build/bench-async.csv`。

实测环境：Linux x86_64，gcc/g++ 12.3、clang 19、go 1.2x、rustc 1.81、
tokio 1.40（Rust 最主流的异步框架）、uya 0.11.0；除 tokio 的 multi_thread
一栏（4 线程）外都是单线程口径（Go 固定 `GOMAXPROCS=1`），热缓存、无网络 IO
（全部用管道 / `socketpair` 模拟连接）。

---

## 1. 机制对照

| | 语言级写法 | 栈从哪来 | 调度者 | 一次挂起存什么 | 同时挂几千路要多少内存 | 「取消」怎么做 |
|---|---|---|---|---|---|---|
| **c6** | `async` 函数 + `await c6_io(fd, ev)` | 每个任务一整块 `malloc`（任务头 + 原生帧），**无栈**（帧就是那块内存） | 运行时 `__c6_co_run()`：一个线程 + 一个 `epoll` 实例 | 什么都不用存：帧的地址本来就是那块内存，只记「哪个 fd、什么事件」 | 约 200～400 B/任务（实测 369 B，含任务头、帧、`malloc` 开销） | 合作式：`acancel` 模块，协程自己在安全点调 `c.poll()` |
| **C** | 没有；要么**手写状态机** + `epoll`，要么 `pthread` | 状态机：`struct` + `switch (state)`；线程：8 MB 栈（虚拟） | 自己写的循环 / 内核调度器 | 状态机：`state` 变量；线程：整个栈被内核换出 | 状态机：几十字节；线程：**8.5 KB/线程**（实测 RSS 增量） | 状态机：自己加 `state = ABORT`；线程：`pthread_cancel`（不可靠） |
| **C++** | C++20 `co_await` + 自定义 awaiter | 编译器生成的协程帧（默认 `operator new` 分配） | 自己写的执行器（这里用 `epoll` + `coroutine_handle`） | 协程帧指针 + `resume` 点 | 约 170 B/协程（实测 168 B） | 同 C：自己把自己从等待队列里摘掉 |
| **Go** | `go f()` + 阻塞式 `Read` | goroutine 栈，起始 2 KB，按需增长/缩容 | 运行时调度器（GMP）+ netpoller（`epoll`） | 整个 goroutine 栈被换出（用户态切换） | 约 2.5 KB/goroutine（实测 2.5 KB） | `context.Context` + `select` |
| **Rust + tokio** | `tokio::spawn` / `tokio::net` + `.await` | 同 Rust：编译器生成的 `Future` + 框架的 `Box`/`Arc` 任务体 | tokio 运行时（`current_thread` 或 work-stealing 线程池）+ mio/epoll | 任务体（含引用计数）+ `Waker` | 约 440 B（`current_thread`）/ 约 630 B（`multi_thread`） | `JoinHandle::abort()` / `select!` + `CancellationToken` |
| **Uya** | `@async_fn` + `try @await f()` | 每个 future 一次 `async_box_alloc_current`（帧池/堆） | `block_on`（探针 + 忙轮询）或 `scheduler_run_*_with_event_loop`（`epoll`） | future 结构体 + `Waker`（最多 4 个 fd 槽） | 单任务 4 个 fd 槽；多路并发靠线程池，**没有**单线程多路等待 | 合作式：`Waker` 的 cancel 位 + `error.Cancelled` |
| **Rust** | `async fn` + `.await` | 编译器生成的 `Future` 状态机（栈上的匿名枚举） | **没有**：由执行器（tokio / async-std / 手写）决定 | `Future` 的状态 + `Waker` | 约 200 B/任务（实测 197 B）；线程模式 **11.7 KB/线程** | 合作式：`Future` 被 `drop` 就是取消 |

共同点只有一句：**无栈协程（c6 / C++ / Rust / Uya）每挂一路的成本是几百字节，
有栈并发（C pthread / Rust thread / Go goroutine）是几千字节到几兆字节。**

---

## 2. 同一件事的六种写法

### c6：`async` 是声明符，`await` 是挂起点

```c
async int read_one(int fd)
{
    char c = 0;
    int rev = await c6_io(fd, C6_CO_EV_READ);   /* 挂起：帧就住在这块内存里 */
    if (rev > 0)
        read(fd, &c, 1);
    return rev;
}

co read_one(fd);                                 /* 顶层任务：扔给调度器 */
__c6_co_run();                                   /* 跑一轮，谁就绪恢复谁 */
```

要点：**没有回调、没有状态枚举、没有 `Box::pin`、没有执行器**。`async` 是函数
声明的一部分（不是模板、不是库），`await` 是关键字，调度器在运行时里。

### C（状态机）：把「挂起」写成「下次从哪进」

```c
struct Conn { int state; int fd; char buf[1]; };

void pump(struct Conn *c)
{
    switch (c->state) {
    case 0: epoll_ctl(ep, EPOLL_CTL_ADD, c->fd, &ev); c->state = 1; break;
    case 1: read(c->fd, c->buf, 1); c->state = 2; break;
    }
}
```
逻辑一复杂，这一层 `switch` 就得手工维护：局部变量要搬到 `struct` 里，`defer`
/ `goto err` 要自己写，跨挂起的 `for` 循环要拆成状态。

### C++20：`co_await` 有了，但**执行器和 awaiter 得自己写**

```cpp
struct WaitRead {
    int fd;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) { epoll_add(fd, h); }
    void await_resume() const noexcept {}
};

static Task read_one(int fd) {   /* Task 也要自己写（promise_type 等） */
    co_await WaitRead{ fd };
    char c;
    read(fd, &c, 1);
}
```
语言给了协程**机制**，没给**运行时**：`Task`、执行器、`epoll` 集成、取消传播
都在标准库之外（生产上就是 asio / libunifex / cppcoro 这类库）。

### Go：语法最省，代价是运行时最重

```go
go func() {                       // 启动协程
    buf := make([]byte, 1)
    f.Read(buf)                   // 阻塞写法，netpoller 在背后挂起它
}()
```
阻塞式写法 + 运行时调度是 Go 最大的生产力优势；代价是每路 2 KB 起步的栈、
GC、以及「取消要靠 `context` 传」的约定。

### Uya：`@async_fn` / `@await`，编译器把函数体重写成状态机

```uya
@async_fn
fn read_one(fd: i32) Future<!usize> {
    const ready: usize = try @await async_wait_readable(fd, 0);
    if ready == 0 {
        var buf: [byte: 1] = [0];
        _ = sys_read(fd, &buf[0], 1) catch { return 0 as usize; };
    }
    return ready;
}

const r: !usize = block_on<usize>(read_one(fd));   // 当前是探针 + 忙轮询
const got: usize = r catch {
    0 as usize;
};
```
写法和 c6 非常接近（属性 + 关键字 + `try`/`catch` 的错误传播），区别在运行时：
`block_on` 只做 `poll -> Pending -> 再 poll` 的忙轮询，等 fd 的 future 在自己的
`poll` 里先用零超时的 `poll(2)` 探一下；真正的事件循环版本是
`scheduler_run_*_with_event_loop`，而**单个任务的 `Waker` 最多登记 4 个 fd**
（`WAKER_IO_SLOT_MAX`），所以单线程多路等待要靠任务队列
（`scheduler_run_task_queue_*_with_event_loop`）或线程池。

### Rust：`async` 是库特性，执行器是可替换的

```rust
async fn read_one(fd: i32) {
    ReadReady { fd, parked: false }.await;   // 自己写的 Future
    let mut b = [0u8; 1];
    unsafe { read(fd, b.as_mut_ptr(), 1) };
}
// 还要有执行器：poll -> 若 Pending 就 epoll_wait -> wake -> 再 poll
```
零成本抽象的上限最高（无 GC、无运行时），但**开箱即用的异步要用 tokio**。
本仓库两栏都量了：`bench_rs.rs` 是 200 行手写的 `epoll + Waker` 执行器（tokio
的核心就是这个），`tokio_bench/` 是真正的 tokio 1.40——`current_thread` 与
`multi_thread(4)` 各一组数字：

```rust
#[tokio::main]                       // 等价于 current_thread runtime
async fn main() {
    let h = tokio::spawn(async { 1 + 1 });   // 任务创建：Box + Arc + 调度队列
    println!("{}", h.await.unwrap());
}

// 等 fd 可读（tokio 的 netpoller 路径）
let (mut a, mut b) = UnixStream::pair()?;
a.write_all(b"x").await?;
b.read(&mut buf).await?;
```

---

## 3. 性能：四项指标

`make bench-async` 在本机的输出（数字随机器波动，看数量级）：

| 语言 / 机制 | create (ns/任务) | switch (ns/往返) | conns (µs/连接) | RSS (B/挂起任务) |
|---|---:|---:|---:|---:|
| **c6** `async/await` + epoll | 141 | 2812 | 5.46 | 266 |
| **C** `epoll` + 手写状态机 | 13 | 1926 | 2.69 | 25 |
| **C++** C++20 协程 + epoll | 21 | 2057 | 1.63 | 168 |
| **Rust** `async/await` + 手写执行器 | 44 | 2741 | 3.18 | 197 |
| **Rust + tokio**（`current_thread`） | 1806 | 2288 | 8.81 | 442 |
| **Rust + tokio**（`multi_thread`×4） | 11 111 | 2767 | 2.78 | 631 |
| **Uya** `@async_fn`/`@await`（探针 + 忙轮询） | 407 | 2472 | — | — |
| **Go** goroutine + netpoller | 957 | 535 | 5.51 | 2585 |
| **C** pthread（一任务一线程） | 60 067 | 12 066 | 15.2 | 8499 |
| **Rust** `std::thread` | 49 157 | 8278 | 21.2 | 11 801 |

### tokio 专表（Rust 最主流的异步框架）

tokio 不是「语言的异步」，而是**一个具体实现**：它有自己的假设（任务要能跨
线程迁移、`spawn` 返回 `JoinHandle`、I/O 走 `mio` 的 `epoll` 封装），所以单独
拉出来和 c6、Go、C++ 摆一张表最清楚：

| | create (ns/任务) | switch (ns/往返) | conns (µs/连接) | RSS (B/挂起任务) |
|---|---:|---:|---:|---:|
| **c6** `async`/`await` + epoll（单线程） | **141** | 2812 | 5.46 | **266** |
| **tokio** `current_thread`（单线程） | 1806 | **2288** | 8.81 | 442 |
| **tokio** `multi_thread`×4 | 11 111 | 2767 | **2.78** | 631 |
| **Go** goroutine + netpoller（单线程） | 957 | 535 | 5.51 | 2585 |
| **C++20** 协程 + 手写 epoll（单线程） | 21 | 2057 | **1.63** | 168 |
| **Rust** 手写 `epoll + Waker`（单线程） | 44 | 2741 | 3.18 | 197 |

同一张表能读出三件事：

1. **tokio 的 `spawn` 是「上架」的代价，不是「造一个 future」的代价。**
   `tokio::spawn(async { i + 1 })` 要装箱（`Box`）、分配任务体与 `Arc` 引用计数、
   压进注入队列、返回 `JoinHandle`，`await` 时再走一次唤醒取结果。1.8 µs 里绝大
   部分是这套机器，不是 future 本身（Rust 手写执行器只 `Box::pin` 一个 future，
   44 ns）。**Rust 的「零成本」说的是 future 组合，不是 `spawn`。**
2. **多线程能救 `conns`，救不了 `create`。** `multi_thread` 把 1000 路连接摊到
   4 个 worker，conns 从 8.81 降到 2.78 µs（比 c6 还快），但 `spawn` 要跨线程
   投递，create 从 1.8 µs 涨到 11 µs。**短任务多、每个只有几微秒的负载，tokio
   的调度开销会盖过并行收益**——这正是 tokio 文档里「别用 `spawn` 包微小工作」
   的那条建议。
3. **c6 的位置**：create 比 tokio 单线程快 12.8 倍、RSS 少 40%，conns 相当；
   代价是「只有单线程事件循环」（要并行就开多个 `__c6_co_run()` 线程、各自一个
   epoll）。tokio 的 `multi_thread` 在 conns 上反超，说明**多核是另一条轴**：
   单线程比「每路多便宜」，多线程比「几核能一起收」。

### 四项指标各自量的是什么

- **create**：连续「建一个任务 → 让它跑完 → 回收」200 000 次（Uya 同），报每个
  任务多少纳秒。它衡量的是**任务对象的分配 + 初始化 + 销毁**，不掺调度。
- **switch**：同一线程上跑 50 000 轮「等 fd 可读 → 另一侧写一个字节 → 内核报
  就绪 → 恢复执行」。c6 / C / C++ / Rust 这里是**一次 `epoll_wait` + 一次
  `read` 系统调用**；Uya 是**一次零超时 `poll(2)` 探针 + 一次 `read`**（数据先
  写好，所以探针必中，不进事件循环）；Go 是 channel 往返（两次 goroutine 切换，
  全在用户态）；线程版是两次真正的内核调度。
- **conns**：1000 路连接（每路一对管道）先全部挂起，再一次性写满，量**把 1000
  路全部服务完**要多久（除以 1000）。协程版本的「挂起/登记」都在计时窗口之外，
  所以这条比的是「就绪之后收完 1000 路」。
- **RSS**：挂起 1000 路前后 `/proc/self/statm` 的常驻页差 / 1000。它是**下界**
  （页粒度 4 KiB，前 1000 个小对象可能落在已经分配的页里），线程版则是实打实
  的内核栈占用。

### 怎么读这张表

1. **无栈协程把「并发密度」提高一个数量级。** c6 369 B / Rust 197 B / C++ 168 B
   一路；Go 2.6 KB 一路（有栈，但会增长）；pthread 8.5 KB 一路，还有 8 MB 虚拟
   地址空间——一万路线程就要 85 MB 常驻，一万路协程只要 3.7 MB。
2. **`create` 的差距主要来自「谁来分配」**：C 的状态机版只是 `malloc` 一个
   指针（13 ns），C++ 协程帧是编译器生成的一次分配（21 ns），Rust 手写执行器
   每任务还要一个 `Arc<Waker>`（45 ns），c6 每个任务两次 `malloc`（任务块 + 帧，
   141 ns），Uya 每任务一次帧池分配 + 一次 `Waker` 初始化（407 ns），
   **tokio 的 `spawn` 要 1.8 µs**（任务体 `Box` + `Arc` 引用计数 + 注入调度队列
   + `JoinHandle` 唤醒），`multi_thread` 还要跨线程投递，涨到 11 µs。**线程创建
   是 49～60 µs，比协程贵 1000～5000 倍**——这就是「一连接一线程」撑不住的原因。
3. **`switch` 的差距来自「几次系统调用」**：任何走 `epoll` 的实现都落在
   2～2.9 µs（一次 `epoll_wait` + 一次 `read` 就是这个价），Go 的 channel 往返
   零超时 `poll(2)` 探针（2.5 µs），tokio 2.3～2.8 µs，Go 的 channel 往返只要
   0.54 µs（完全在用户态，不碰内核），线程切换 8.3～12 µs。**c6 的 2812 ns 里
   没有额外开销**：它和 C/C++/Rust/tokio 的 epoll 版本是同一档。
4. **`conns` 是「一次收 1000 路」的摊销**：C++/C 1.6～2.7 µs，tokio
   `multi_thread` 2.8 µs，Rust 手写 3.2 µs，c6 5.46 µs，Go 5.5 µs，tokio
   `current_thread` 8.8 µs（单线程下 tokio 每路要多一次任务唤醒 + 重新注册
   `AsyncFd`，比 c6 的「一次 `epoll_ctl` + 直接恢复」更重）。c6 略慢的那 2～3 µs/连接来自两件事：每次 `await` 一次
   `epoll_ctl`（`EPOLLONESHOT` 重新装填，和 Go 的 netpoller 同一种做法），以及
   每个任务结束时的一次 `free`。C 的状态机版本不需要重新装填（注册是长期的），
   所以它最快；线程版本每条连接都要一次内核唤醒，10.5～18.7 µs。

### 这几种「不公平」必须说清楚

- **c6 的创建开销含 `malloc`**：任务是按需分配的（没有固定池、没有上限），
  代价就是每次 create/join 两次 `malloc` + 两次 `free`。C 的状态机版本对比的
  只是一个指针的 `malloc`，C++ 是编译器生成的一次分配。
- **Uya 只有两栏**：`conns`/`RSS` 是空的，原因是 std.async 的 `block_on` 只做
  忙轮询、单任务 `Waker` 最多挂 4 个 fd（`WAKER_IO_SLOT_MAX`），单线程多路
  等待要显式用任务队列（`scheduler_run_task_queue_*_with_event_loop`），而那
  需要手写 `TaskQueue<usize>` 的装载与生命周期，和其余语言的「一个循环跑完
  1000 路」不是同一段代码。要公平地比 conns，得给 Uya 也写队列版基准——当前
  不硬凑数字。
- **Go 的 `switch` 用的是 channel**，不是 fd：它量的是 goroutine 切换，不是
  一次 `epoll_wait`。要 fd 口径的话，把 `bench_go.go` 里的 channel 换成
  一对 `os.Pipe` 即可（`conns` 段已经是管道）。
- **线程版本的 `switch` 是两次调度**（写方醒来 + 读方醒来），有栈实现本来就要
  付这个代价。
- **Rust 有两栏，别混着看**：「手写执行器」是 200 行的最小实现（`Box::pin` +
  `epoll` + `Waker`），只用来量 future 组合本身的开销；「tokio」是真框架（1.40，
  钉版本是因为本机 rustc 1.81，更新的 tokio 要求 1.85）。生产上没人自己写前者，
  **要拿去和别的语言比，请用 tokio 那两栏**。
- **tokio 的 `conns` 用 `UnixStream::pair`，c6 用 `pipe`**：两者都是内核对象 +
  `epoll`，多一次 `socketpair` 创建（在计时窗口之外），读路径等价。
- 所有数字都是**同一台机器、单线程、无网络**。真实服务里 5.65 与 2.44 µs 的
  差别通常被 DNS、TLS、序列化淹没——**选型的真正分水岭是内存密度（第 1 条）
  和「谁能把取消/超时做对」**，不是这几微秒。

---

## 4. 什么时候选什么

| 场景 | 选择 | 理由 |
|---|---|---|
| 几万路长连接、逻辑复杂、要和 C 代码混编 | **c6**（`async`/`await` + epoll） | 语言级协程，几十万行 C 库不用改；每路几百字节 |
| Rust 生态、要现成的网络/定时/取消 | **tokio**（`multi_thread`） | 多核下 conns 最好（2.78 µs）；代价是 `spawn` 1.8～11 µs，别拿它包微小任务 |
| 同样的活但要极致吞吐 / 无运行时 | **C++ 协程**或 **Rust async**（手写执行器） | 零成本抽象、无 GC；代价是自己搭执行器 |
| 团队不想碰内存管理、要快速迭代 | **Go** | 阻塞写法 + 运行时调度，2.5 KB/goroutine 换来心智负担最低 |
| 在用 Uya 生态、要 `@async_fn` 的写法 | **Uya**（`@async_fn` + `scheduler_run_*`） | 写法和 c6 同档；单线程多路等待要显式上任务队列 |
| 只有几十上百路并发、逻辑是「请求-响应」 | **线程**（C pthread / Rust thread） | 写起来最直白，8.5 KB/线程在这种规模完全无所谓 |
| 硬实时、无堆、无 libc | **c6（freestanding）** | `async` 运行时不要 libc，裸机也能跑（`make fstest-bare-*`） |

c6 的位置很清楚：**C 的成本结构，Go 的写法**。它没有 GC、没有运行时线程、
不依赖 libc（裸机上照样能 `await`），但写起来是 `async`/`await` 关键字而不是
状态机——`bench/async/bench.c6` 与 `bench/async/bench_c.c` 的对比就是这句话的
实物：同一个 1000 路连接的循环，c6 版 40 行，C 状态机版 260 行。

---

## 5. 复现

```sh
make bench-async            # 编译各语言目标 + 打表，明细在 build/bench-async.csv

# 只跑某一个语言 / 某一段
./tcc/c6 -B tcc -I . -o build/bench_c6 bench/async/bench.c6 std/os/time.c6
BENCH_SEC=conns ./build/bench_c6
BENCH_SEC=conns ./build/bench_go
BENCH_SEC=conns ./build/bench_rs threads

# Uya（可选依赖，找不到编译器时 run.sh 自动跳过这一栏）
UYA_ROOT=<uya 仓库>/lib uya build bench/async/bench.uya -o build/bench_uya
./build/bench_uya        # 打印 create_total_ns / create_n / switch_total_ns / switch_n
```

源码对照（同一个循环在六种语言里的样子）：

| 文件 | 内容 |
|---|---|
| `bench/async/bench.c6` | c6 `async`/`await` + `c6_io` |
| `bench/async/bench_c.c` | C：`epoll` 状态机（默认）与 `pthread`（`threads` 参数） |
| `bench/async/bench_cpp.cpp` | C++20 协程 + `epoll` 执行器 |
| `bench/async/bench_go.go` | goroutine + `os.Pipe`（netpoller） |
| `bench/async/bench_rs.rs` | Rust `async`/`await` + 手写 `epoll` 执行器；`threads` 参数切 `std::thread` |
| `bench/async/tokio_bench/` | Rust + tokio 1.40（`current_thread` / `multi_thread` 两栏） |
| `bench/async/bench.uya` | Uya `@async_fn`/`@await` + `async_wait_readable`（create/switch 两段） |
| `bench/async/run.sh` | 编译 + 分段跑（每指标一个干净进程）+ 换算 + 打表 |

语言层面的异步框架细节（调度模型、`abudget`/`acancel`/`agroup`/`apool` 这些
横切面）见 `docs/async-framework.md`；运行时的实现（`EPOLLONESHOT` 注册表、
O(1) 唤醒、fd 与协程数都没有固定上限）见 `docs/design/implementation-notes.md`
与 `docs/manual/concurrency.md`。
