# 托管引用（引用计数 GC）

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> `@T` / `weak` / `own` / `shared` / `drop`：引用计数在对象头里，所有权由编译器静态检查，环是硬错误。
>
> 相关：示例 `examples/gc_demo.c6`；用例 `tests/gc_return.c6` 等 `tests/gc*.c6`；设计 [gc-design.md](../design/gc-design.md)

---

## 托管引用（引用计数 GC）

`@T` 是**托管引用**（指向 `T` 的裸指针，多了一个 `@`）：对象的生命由引用
计数决定，最后一个引用消失时释放。没有标记清扫、没有 STW —— 计数就在对象头
里，加减都是普通的内存写，只有 `shared` 才走原子指令。

```c
struct Node { int v; own @Node next; };

void f(void)
{
    @Node a = new Node;      /* 计数 1，作用域退出时自动释放 */
    @Node b = new Node;
    a->v = 1;
    b->v = 2;
    a->next = move(b);       /* 独占链：b 的槽交出去，链由 a 带着走 */
    a = NULL;                /* 想提前放手就赋空，不需要 reset() */
}                            /* a 的链在这里整条释放 */
```

| 写法 | 含义 |
|---|---|
| `@T p` | 强引用，计数，可以有多个 |
| `shared @T p` | 强引用，计数是**原子**的，可以跨线程；对象要用 `new shared T` 建 |
| `weak @T p` | 弱引用，不计数；解引用前必须先升级（`@T q = p;`） |
| `own @T p` | 独占，不计数；持有者死亡时整条链递归释放；只能接 `new` 或 `move()` |

规则：

- 托管引用只能存进托管槽（局部、字段、元素、全局都算）；`T *p = managed;`
  只允许**立即使用**，保存会被拒（借出的指针没有计数，保存下来就是 UAF），
  新引用存进裸指针同样报错；
- 含托管字段的结构体不能按值拷贝/传参/返回（`memcpy` 也是），只能 `new`；
- 形参是**借用**：被调者不计数、不释放，也不能改写（要留住就拷进自有局部
  `@T m = n;`）；`async`/`spawn` 的边界只能传 `shared @T`；
- `move(p)` 之后 `p` 的槽就空了：在重新赋值之前再读它、比较它、解引用成员、
  传给函数、或者再 `move` 一次，都是**编译错误**（E-GC17）——空值解引用就是
  运行期崩溃。重新赋值（`p = new T` 或 `p = NULL`）之后照常使用；
- **环是编译错误**。类型图上出现自环或互环直接报错，出路只有两个：把反向
  边改成 `weak`，或者把子链改成 `own`。这也顺带把「父→子强、子→父弱」和
  「树用 own」逼成了默认写法；
- 析构函数写成方法 `void drop(struct T *self)`：最后一个引用消失时跑一次，
  在字段释放**之前**，不能失败、不能挂起、不能手动调用。

```c
struct File { int fd; };
void drop(struct File *self) { close(self->fd); }
```

`async`/`spawn` 的任务**自己持有**一份参数和返回值（进任务 +1、任务销毁 −1），
所以「创建者先放手、任务后跑完」是安全的；没跑完就被销毁的任务会回卷
（跑 `errdefer`/`defer`/释放），不会漏引用。

完整可运行示例见 [examples/gc_demo.c6](../../examples/gc_demo.c6)：作用域释放与
`p = NULL`、`new T[n]` 的逐元素析构、`own` 子树配 `weak` 父指针、`move()`、
`shared` 跨线程与 `spawn`、`async` 任务持有参数和返回值，最后用
`__c6_gc_live()` 自证「全程没有泄漏」（`rc=0` 才算过）：

```sh
./tcc/c6 -B tcc -run examples/gc_demo.c6
```

代价与尺寸（x86_64，热缓存，跨 TU 不内联）：`@T` 的一次加/减约 2.2 ns，
`shared @T` 约 8.4 ns（原子指令 + helper 调用）；每个对象 32 字节头；
作用域退出时释放是**内联**的，约 2.5 ns/引用。对照同机 `std::shared_ptr`
的 6.3 ns/操作：单线程 `@T` 更快，`shared @T` 同量级。

和 C++ 智能指针的差别：`@T` 就是裸指针（8 字节、能过 C ABI，`shared_ptr`
是 16 字节加一个控制块）；拷贝被编译器限制在托管槽之间（`shared_ptr` 随手
可拷）；环不会静默泄漏 —— 编译期就报错；另外 `p = NULL` 就是 `reset()`，
不额外引入成员函数。详细的设计与取舍见 [docs/design/gc-design.md](../design/gc-design.md)。
