# c6 的语言级异步框架（完整版）

> **状态**：现行 · **层**：运行时 / 标准库 · **索引**：[文档索引](README.md) ·
> **相关**：[并发语法](manual/concurrency.md)、[grammar.md §3.4–3.6](reference/grammar.md)、
> [stdlib.md](stdlib.md)

`async` / `await` / `co` / `task` / `spawn` 是**语言关键字**，不是库；
`std/async/` 把这些关键字组织成一套能直接用的框架：I/O 原语、通道、驱动、限流、
取消、预算、指标。这篇文档讲**整体怎么搭起来的**，每个模块的 API 细节看
[stdlib.md](stdlib.md)。

## 1. 分层

```
        用户代码            async int serve(void *st) { ... await c6_io(fd, EV); ... }
                            co serve(&st);        task t = serve(&st);
        ─────────────────────────────────────────────────────────────────────
        组合层    abudget(预算/让出)  agroup(任务组)  apool(额度)
                  acancel(协作取消)  scope(结构化并发)  limiter(排队限流)
        ─────────────────────────────────────────────────────────────────────
        原语层    aio(文件描述符读写)  achan(通道)  apump(流式转发)  httpsa(异步 HTTPS)
        ─────────────────────────────────────────────────────────────────────
        驱动层    art(Rt：wait/run/destroy 统计)   abench(开销指标)
        ─────────────────────────────────────────────────────────────────────
        运行时    __c6_co_new/wait/run/destroy/state/kind   c6_io(fd, ev)   选择器用 select(2)
```

**调度模型**：无栈协程 + 单线程事件循环。`await c6_io(fd, ev)` 把当前任务挂到
`(fd, 读/写)` 上并让出，`__c6_co_run()` 用一次 `epoll_wait(2)` 等所有挂起点，谁就绪恢复谁。
所以：

- 一个任务一次只挂在一个 fd 上，但**没有数量上限**：同时存活的协程数和 fd 号都
  只受内存与 `RLIMIT_NOFILE` 约束（挂起用 `epoll(7)`，不是 `select(2)` 的 1024 窗口）；
- 协程不可被抢占，**超时/取消只能由协程自己在安全点检查**（这就是 `abudget`/`acancel`）；
- 真并发要显式 `spawn`（`pthread`），`thread h = spawn f(); h->join()`。

## 2. 三种启动方式，按生命周期选

| 写法 | 句柄 | 谁回收 | 用途 |
| --- | --- | --- | --- |
| `co f(args);` | 无 | 运行时（跑完自动销毁），数量只受内存限制 | 顶层「发出去就不管」 |
| `task t = f(args);` | `t`，读结果 `t->val` | 作用域退出自动 `__c6_co_destroy` | 要结果、要 `__c6_co_wait` |
| `spawn f(args);` | `thread h` | 显式 `h->join()` | CPU 并行（真线程） |

`__c6_co_wait(t)` 把任务驱动到完成并返回结束码，`__c6_co_run()` 驱动**全部**剩余任务、
返回失败个数。`async` 函数**不能**是方法（`error: 'async' functions cannot be methods`），
所以框架里所有 async 函数第一个参数都是 `void *`（句柄/状态指针），状态机放在
`struct Xxx` 里，用**结构体方法**提供同步部分（读数、复位、统计）。

## 3. 一页速查

```c
import std.async.aio;      import std.async.achan;
import std.async.abudget;  import std.async.agroup;   import std.async.apool;
import std.async.acancel;  import std.async.scope;    import std.async.limiter;
import std.async.art;      import std.async.abench;   import std.async.apump;

/* I/O */
struct aio_read_task *r = aio_read(fd, buf, cap);      // await → 读到的字节数
co aio_write_all(fd, "data", 4);                       // 顶层发出去

/* 通道 */
AChan ch = { 0 };  ch.init();
co achan_send(&ch, buf, n);   int got = await achan_recv(&ch, out, cap);

/* 额度与任务组 */
APool p = { 0 };   p.init(4);
if (p.acquire()) { AGroup g = {0}; g.reset(); g.add(aio_read(...)); g.join(); p.release(); }

/* 预算（合作式超时）与取消 */
Budget b = { 0 };  b.start(50);
while (!b.expired()) { if (c.poll()) break; await ayield(); }

/* 指标 */
Bench m = { {0}, 0, 0, 0, 0, 0 };
m.run_null(1000); m.run_io(500, rfd, wfd); m.run_co(500, rfd, wfd);
char line[192] = {0}; m.report(line, sizeof line); printf("%s\n", line);
```

## 4. 实测开销（x86-64 + TCC 后端，`cmake --build build --target std-https` / `cmake --build build --target std-async5`）

| 场景 | 吞吐 | 单次 |
| --- | --- | --- |
| 任务创建 + 驱动 + 销毁（无 I/O） | ≈ 5.8 M ops/s | **≈ 170 ns** |
| 一次真正的挂起 + 恢复（`await c6_io`，含 `epoll_wait`+`read`） | ≈ 0.4 M ops/s | **≈ 2.5 µs** |
| `co` 顶层任务批量调度（`__c6_co_run`） | ≈ 0.5 M ops/s | **≈ 2.0 µs** |

读法：**框架本身的开销是一次系统调用**。挂起/恢复的 2.5 µs 里绝大部分是
`epoll_wait(2)` + `read(2)`，协程切换只是几个字段的赋值；所以「一线程挂几千路连接」
摊到每路上的成本就是一次 `epoll_wait` 的摊销，而不是每路一个栈（无栈协程的栈按需分配、
任务帧就是一次 `malloc`）。

复现：`cmake --build build --target std-https`（HTTPS 三行指标）、`cmake --build build --target std-async5`（框架横切面 + 三行指标）。

## 5. 这套框架**没有**做的事（诚实清单）

- **时间轮**：`await` 只能等 fd，不能等时间；要做真超时得自己起一个定时 `spawn` 线程
  写自唤起管道，或者用 `std.os.reactor` 的定时器循环（那一套是协程模型，不和
  `async` 关键字混用）；
- **跨线程唤醒**：`c6_io` 的选择器在本线程 `select` 上，别的线程把数据写进管道能唤醒
  它，但任务的完成回调不跨线程；
- **取消**是合作式的：`ACancel.poll()` 只有在协程自己调用时才生效，没有「强杀」；
- **一个任务只能挂一个 fd**：要同时等多路，得用 `achan` 把多路合成一路，
  或者用 `agroup` 分别驱动。

## 6. 与另两套并发设施的关系

| 设施 | 调度 | 何时用 |
| --- | --- | --- |
| `async`/`await`/`co`/`task`（本篇） | 无栈协程，单线程 `select` | I/O 密集、连接数多的服务；写起来最像同步代码 |
| `std/os/reactor` + `std/async/co`（`c.start`/`c.join`） | 手写状态机 + 执行器 `tick` | 需要精确定时器、或在没有 `async` 关键字的场合复用 |
| `spawn` / `std/os/thread` | pthread | CPU 密集、要真并行，或阻塞式库 |

三者可以混用：`spawn` 出去的线程里跑自己的 `__c6_co_run` 循环，互不干扰。
