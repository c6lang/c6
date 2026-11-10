# c6 语言扩展语法规范（BNF / EBNF）

本文件用 **ISO/IEC 14977 扩展巴科斯范式（EBNF）**给出 TinyCC 的 c6
扩展相对 ISO C 的**增量语法**。标准 C 的语法不再重复；文中出现的
`declaration`、`statement`、`expression` 等未定义非终结符一律指 ISO C
（C11 / N1570）中的同名产生式。

扩展只在编译期定义了 `C6_EXT` 且**源文件后缀是 `.c6`** 时生效（后缀是唯一
的开关，命令行没有 `-std=c6`）；此时
预处理器额外定义宏 `__C6__`。未开启时（所有 `.c` 文件），下列新关键字全部
退化为普通标识符，本文档描述的产生式一条都不适用。

---

## 1. 记法

| 记号 | 含义 |
|---|---|
| `=` | 定义 |
| `;` | 产生式结束 |
| `\|` | 择一 |
| `,` | 连接 |
| `[ ` … `]` | 可选 |
| `{ ` … `}` | 重复 0 次或多次 |
| `( ` … `)` | 分组 |
| `(* … *)` | 注释 |
| `"…"` | 终结符（字面记号） |
| `'…'` | 上下文中的说明性引用 |

词法记号：

```
lower-letter  = "a" | "b" | … | "z" ;
upper-letter  = "A" | "B" | … | "Z" ;
digit         = "0" | "1" | … | "9" ;
letter        = lower-letter | upper-letter | "_" ;
identifier    = ( letter | "_" ) , { letter | digit } ;

(* 扩展新增的关键字 *)
c6-keyword =
      "package" | "import" | "interface"
    | "defer"   | "errdefer" | "err" | "test"
    | "try"     | "catch"
    | "async"   | "await"
    | "foreach" ;

(* 软关键字：只在语句开头、且后面紧跟一个标识符时才是关键字 *)
c6-soft-keyword = "co" | "go" | "task"
                | "module" ;   (* 只在一个文件的第一个声明之前 *)

(* 扩展内建的错误值类型，由编译器预置的 typedef 提供，等价于
   typedef struct __tcc_error { int id; const char *msg; } error;
   'error' 只能出现在返回类型里（作为 'T!error' 的错误载荷），
   不能作为函数参数，见约束 C18。 *)
error-type-name = "error" ;
```

切片类型 `T[:]` 不引入关键字：它由已有的 `[`、`:`、`]` 三个记号组成，
只在「声明说明符之后」这个 C 里本来非法的位置被识别（见 3.3）。

`try`、`catch`、`err`、`defer`、`errdefer`、`package`、`import`、
`interface`、`foreach` 只会作为关键字出现在下列产生式允许的位置；在其余
位置（例如声明说明符中）它们仍可被当作标识符使用，这一点与 C 关键字
不同，是单趟实现的已知宽松之处。`impl` 自 0010 起不再是关键字。

`foreach` 还多一条限制：它只在**语句开头**、且下一个记号是 `|` 时才算
语句（分派在 `block()` 里做，此时下一个记号已经在手），所以
`int foreach = 1;`、`foreach = 2;`、`x = foreach;`、`foreach(x);`、
`p->foreach`、`goto foreach;` 全部照常工作。空的绑定表 `||` 是逻辑或，
不会命中这条语句，仍按表达式解析。

`async`、`await` 不享受这条宽松：它们在词法阶段就是关键字，所以
`int async;`、`int await;` 会报错（成员名不受影响：
`struct S { int async; }; s.async` 仍然合法，因为成员查找比较的是记号值）。

`co`、`go`、`task` 是**软关键字**，词法阶段仍是普通标识符，只在语句开头
且后面紧跟一个标识符时才被当作关键字（判定发生在语句分派处，此时下一个
记号已经在手）：

```c
int go = 1;          /* 变量：'go' 后面是 '=' */
go = 2;              /* 赋值 */
x = go;              /* 取值 */
go(x);               /* 调用名为 go 的函数：后面是 '(' */
typedef int co;      /* 类型名 */
co v;                /* 声明：'co' 是 typedef 名，不是 co 语句 */
task:                /* 标签：后面是 ':' */
go f(a);             /* 语句：'go' 在语句开头且后面是标识符 -> 线程 */
co f(a);             /* 同理 -> 顶层协程 */
task t = f(a);       /* 同理 -> 句柄 */
```

形状相同的两个意思（局部变量 `go` 后面紧跟一个标识符、且这一行又是语句
开头）无法区分，此时按关键字处理。`typedef` 名会让软关键字让路：
`co` 已经被声明成类型名时，`co v;` 一定是声明。

---

## 2. 文件作用域

```
translation-unit       = { top-level-declaration } , EOF ;

top-level-declaration  = module-declaration
                       | package-declaration
                       | import-declaration
                       | interface-declaration
                       | test-declaration
                       | declaration ;

(* ---- module ---- *)

module-declaration     = "module" , package-name , ";" ;
                         (* 必须是文件的第一个声明；'package-name' 的点号
                            形式与 package 一样组合前缀，见 2.1 *)

(* ---- package / import ---- *)

package-declaration    = "package" , package-name ,
                         ( "{" , { package-item } , "}" | ";" ) ;
                         (* 点号形式与嵌套块、文件级形式等价地组合前缀：
                            'package a.b' 与 'package a { package b { … } }'
                            都产生前缀 'a__b' *)

package-name           = identifier , { "." , identifier } ;

package-item           = package-declaration
                       | function-definition
                       | declaration ;

import-declaration     = "import" , package-name , ";" ;
                         (* 打开 'pkg.member' 写法，必须出现在使用点之前；
                            若找到对应的 'NAME.c'（点号形式为 'a/b.c'），
                            还会加载它的声明，见 2.1 *)

(* ---- interface ---- *)

interface-declaration  = "interface" , identifier , "{" ,
                             { method-declaration } ,
                         "}" ;

method-declaration     = declaration-specifiers , identifier ,
                         "(" , [ parameter-type-list ] , ")" , ";" ;
                         (* 只能是函数声明，不能带函数体 *)

(* 结构体如何实现接口没有专门的声明语法：文件作用域里第一个参数是指向
   该结构体（或联合体）指针的函数，就是它的方法，见 3.2 与 C20/C21。 *)

(* ---- test ---- *)

test-declaration       = "test" , string-literal , compound-statement ;
                         (* 等价于 static int __tcc_test_N(void) ，
                            返回值 0 表示通过；-test 运行，-run 时忽略 *)
```

---

## 3. 声明与 `!error` 后缀

```
function-declaration   = declaration-specifiers ,
                         [ fallible-suffix ] ,
                         declarator ,
                         [ fallible-suffix ] ;
                         (* 后缀也可以紧跟在声明符内某个 '*' 之后 *)
                         (* 两个位置等价，可任选其一；不得同时出现在
                            同一个声明符的非函数层级上 *)

fallible-suffix        = "!" , "error" ;

function-definition    = declaration-specifiers ,
                         [ fallible-suffix ] ,
                         declarator ,
                         [ fallible-suffix ] ,
                         compound-statement ;
```

语义：`fallible-suffix` 把被声明的**函数类型**标记为“可能失败”。
后缀可以出现在三处，含义完全相同：

```
    int!error f(void);          (* 类型说明符之后 *)
    int *!error f(void);        (* 指针声明符的 '*' 之后 *)
    int f(void)!error;          (* 完整声明符之后 *)
    int *f(void)!error;         (* 同上，返回 int* 的失败函数 *)
    int (*fp)(void)!error;      (* 指向失败函数的函数指针 *)
```

约束 C8 要求被标记的最终类型必须是一个函数（或指向函数的指针）。

`error` 本身是**只出不进**的类型：它可以出现在返回类型里，但不能出现在
形参表里（`error`、`error*`、`error[]` 都算），因为错误只能通过返回值
离开函数，不能通过实参传进来或用出参带回：

```
    error  make(void);              (* 合法：作为返回值 *)
    int!error f(void);              (* 合法：'!error' 是个标记，不是形参 *)
    void   take(error e);           (* 不合法 *)
    void   take(error *e);          (* 不合法：出参形式 *)
    void   take(error a[]);         (* 不合法 *)
```

函数体内的 `error` 局部变量是允许的（`catch` 的处理变量就是这种形式），
但它同样是变量定义，必须带初始化器：`error e = {0};`（约束 C19）。

`!error` 同样适用于函数指针的声明符：

```
    int (*fp)(void)!error;      (* fp 指向“可能失败的 int()” *)
    typedef int fn_t(void)!error;
```

前置声明带 `!error` 时，后续不带后缀的定义会被自动标记（同一符号）。

### 3.1 变量定义必须初始化

约束 C19 要求**每个变量定义都带初始化器**，`static` 变量与 VLA 除外：

```
    int g = 0;                      (* 文件作用域的定义也要写 *)
    static int counter;             (* 合法：static 变量豁免 *)

    void f(void)
    {
        int i = 0;
        int a[3] = {1, 2, 3};
        struct S s = {0};
        static int calls;                (* 合法：块作用域的 static 同样豁免 *)
        extern int declared_elsewhere;   (* 合法：声明，不是定义 *)
        int vla[n];                      (* 合法：VLA 不允许写初始化器 *)
    }
```

规则在**定义点**检查，不做数据流分析（`int x; x = 1;` 一样报错）。
下列构造不是“变量定义”，因此不受约束：`extern` 声明、函数形参（含 K&R
形参声明表）、结构体/联合体成员、`typedef`、函数声明与定义。
两类豁免：`static` 变量（文件作用域或块作用域）——`static` 声明本身就是
「这是一份常驻状态」的写法，标准已经保证静态存储期对象零初始化
（C11 6.7.9p10），写 `= 0` 只是噪声，而且不带初始化器的对象落在 `.bss`、
不占输出文件；VLA 则是因为标准 C 不允许给变长数组写初始化器。

诊断里使用源码中书写的标识符：`package` 内的变量报原名，而不是
`NAME__x`。

这条约束只作用于 `.c6` 的翻译单元：`.c` 文件的老代码完全不受影响；一旦开启，该 TU 里出现的所有定义（含包含进来的头文件里的
函数体）都要满足。K&R 老式定义的形参声明表属于形参，不受约束。

### 3.2 结构体方法

文件作用域的函数声明或定义，只要**第一个参数是指向结构体（或联合体）的
指针**，就同时注册为该类型的方法：

```
    struct Vec { int x, y; };

    int vec_sum(struct Vec *self) { return self->x + self->y; }

    void f(struct Vec *p) { p->vec_sum(); }   (* 等价于 vec_sum(p) *)
```

方法**不是**新的函数类型：调用约定不变，`vec_sum(p)` 与 `p->vec_sum()` 等价，
接收者就是第一个实参。区别只在**符号名**：方法以 `<结构体>_<方法>` 的名字输出
（上面的 `vec_sum` 是 `Vec_vec_sum`），源码里写的名字另外输出成一个**弱别名**，
因此：

```
    struct Circle { int r; };
    struct Rect { int w, h; };

    int area(struct Circle *c) { return 3 * c->r * c->r; }  (* Circle_area *)
    int area(struct Rect *r)   { return r->w * r->h; }      (* Rect_area *)

    void f(struct Circle *c, struct Rect *r)
    {
        c->area();          (* Circle_area(c) *)
        r->area();          (* Rect_area(r) *)
        area(c);            (* 弱别名 area：本 TU 里第一个叫 area 的方法 *)
    }
```

- 不同类型可以用同名方法，不必放进不同 `package`；
- `area(p)` 这种普通调用仍然可用，指向本翻译单元里**第一个**取到该名字的方法；
  真正有歧义时它只是个弱别名，链接时会被手写的强定义覆盖。纯 C 的翻译单元
  按原名链接不受影响；
- 普通名已经被别的函数占用时不发别名：同名的普通函数、系统头里声明的 libc
  函数（`getpid`）都不受方法影响，这样的方法只能写 `p->f()` 调用；
- 下列情况不改名，方法符号就是源码里的名字：`static` / `inline` 函数、
  以 `__` 开头的名字（编译器和 libtcc1 专用）、来自系统头文件的声明
  （`p->fflush()` 仍然调 libc 的 `fflush`）、匿名结构体的方法（匿名 tag
  的编号是每个 TU 各一份），以及在 `package` 里声明的方法（包名已经把它
  和别的包分开，符号是 `pkg__name`）。

约束见 C20。


### 3.3 切片类型 `T[:]`

```
slice-type-suffix      = "[" , ":" , "]" ;
```

`slice-type-suffix` 紧跟在**基础类型或 typedef 名之后**，是类型的一部分：

```
    int[:] s;                       (* 切片的元素是 int *)
    int[:] *p;                      (* 指向切片的指针 *)
    int[:] a[3];                    (* 3 个切片的数组 *)
    typedef int[:] Ints;            (* 类型别名 *)
    void f(int[:] s);               (* 形参 *)
    int[:] g(void);                 (* 返回值 *)
    struct W { int[:] s; };         (* 成员 *)
    sizeof(int[:])                  (* 2 * sizeof(void *) *)
    (int[:]){ p, n }                (* 复合字面量：指针 + 长度 *)
    int[:][:] nest;                 (* 元素本身是切片 *)
```

C 里 `[` 紧跟类型说明符是语法错误（数组声明符在名字之后），所以
`T[:]` 不占用任何既有写法：`int a[3]`、`int a[] = {…}`、`void f(int[])`、
`typedef int T[]` 全部保持原义。`[]` 中间必须**恰好**是 `:`，写成
`int[3] x;` 依旧是原来的语法错误。

切片值表示为两个机器字：

```
    struct {
        T *const data;              (* 第一个元素，偏移 0 *)
        const size_t len;           (* 元素个数，偏移 PTR_SIZE *)
    }
```

布局固定（不受 `#pragma pack` 影响），因此跨翻译单元只要两边都写
`T[:]` 就能按值传递/返回，不需要额外的头文件声明。

语义（重写规则见第 8 节）：数组在运行期上下文里隐式转成切片；`s[i]`
先检查 `i < len`；`s[lo:hi]` 先检查 `lo <= hi <= len`。越界是运行时
陷阱，不做静态诊断。约束见 C22–C28。

### 3.4 async 函数（无栈协程）

```
async-function-declaration = "async" , declaration-specifiers ,
                             declarator , ";" ;
async-function-definition  = "async" , declaration-specifiers ,
                             declarator , compound-statement ;
```

`async` 是一个**声明说明符**（写法与 `static` / `inline` 同级），把被声明
的函数标记为无栈协程：调用它不执行函数体，只创建一个**任务**；函数体
由 `await` 驱动着一段一段往前跑。

```
    async int add(int a, int b) { return a + b; }

    async int worker(void)
    {
        int got = 0;
        got = await add(20, 22);        (* 挂起，等 add 跑完再继续 *)
        return got;
    }

    int main(void)
    {
        struct worker_task *t = worker();   (* 只是建任务 *)
        int rc = __c6_co_wait(t);           (* 把它跑到完成 *)
        printf("%d\n", t->val);
        __c6_co_destroy(t);
        return rc;
    }
```

调用一个 `async` 函数得到的是 `struct NAME_task *`（**任务句柄**），不是
声明的返回类型。这个结构由声明本身推导出来，因此各翻译单元一致：

```
    struct NAME_task {
        struct __c6_task head;      (* 运行时私有头部，偏移 0 *)
        T                val;       (* 声明的返回值（void 时不存在） *)
        P0               p0;        (* 第 0 个形参 *)
        …
    };
```

`struct __c6_task` 的布局由 `libtcc1` 拥有，生成代码只通过 `__c6_co_*`
访问器碰它；`t->val` 和 `t->pN` 是普通的公开成员。

运行时接口（c6 的翻译单元里由 `__TCC_ASYNC_DEFINED` 预置声明）：

```
void *__c6_co_new(const struct __c6_desc *d);
int   __c6_co_wait(void *task);        (* 驱动到完成，0 = 正常结束 *)
void  __c6_co_destroy(void *task);     (* 释放任务槽（含未完成的子任务） *)
int   __c6_co_state(void *task);
int   __c6_co_kind(void *task);
int   c6_io(int fd, int ev);           (* 只能写成 await c6_io(fd, ev) *)
#define C6_CO_EV_READ  1
#define C6_CO_EV_WRITE 2
```

约束见 C29–C33。

### 3.5 co / task（顶层协程与句柄）

```
co-statement     = "co" , expression , ";" ;
task-declaration = "task" , identifier , "=" , expression ,
                   { "," , identifier , "=" , expression } , ";" ;
```

`co` 和 `task` 是使用 `async` 函数的两种方式（两者都是软关键字，见 §1）：

```
    co worker(1);               (* 顶层、fire-and-forget：没有人等它 *)
    task t = worker(2);         (* 把句柄留在手上 *)
```

* `co f(a);` 建任务，并把它当成**顶层任务**（`parent == 0`）：不属于调用
  者，作用域结束也不回收，交给调度器；`__c6_co_run()` 把它们全部跑完，
  返回其中以 `err` 结束的个数（在 `co` 语句里当场就跑完、当场失败的也
  算在下次 `__c6_co_run()` 的计数里）。
* `task t = f(a);` 建任务、把句柄存进 `t`（类型从初始化器推出，所以必须
  写初始化器），并在**作用域退出时自动 `__c6_co_destroy(t)`**——走的就
  是 `defer` 那套清理路径，所以 `return` / `err` / `break` 出去一样会
  释放。一行可以声明多个（`task a = f(), b = g();`）。
* 两者的操作数都必须是一次 `async` 调用；普通函数用 `go`（§3.6），
  写成 `co` 会明确报错（见 C35）。

**执行顺序**（`co` 唯一的“魔法”）：

`co` 是**同步起点**。任务在 `co` 语句处就开始执行，一直跑到第一个“必须
等待”的点，然后把控制权交还给 `co` 的下一条语句：

```
    co worker(1);           (* worker 跑起来了，可能已经打印过东西 *)
    printf("spawned\n");    (* 然后才轮到这句 *)
```

“必须等待”就是 `await c6_io(fd, ev)` 那种挂起：它总要停一轮，即使 fd 已经
就绪——唤醒是下一轮 `select()` 的事。`await` 一个 `async` 子任务不算等待，
子任务能立刻往下跑就跑完了。所以完全不碰 fd 的 `co f();` 会在 `co` 语句里
一路跑完，并**当场回收任务槽**（没人能观察到它，句柄也没人拿得到）；park
在 fd 上的任务才留给 `__c6_co_run()`。

约束见 C34–C37（`go` 的约束是 C38–C40，见 §3.6）；同时存活的协程最多
64 个（含 `await` 出来的子任务）。
`co` 出去的协程和调用方在同一线程上协作式运行；要真并发用 `go`（§3.6）。

### 3.6 go（OS 线程）

```
go-statement = "go" , expression , ";" ;
```

`go f(a);` 在真正的 OS 线程（`pthread_create`）上运行普通函数 `f`，语句
立刻返回：

```
    go download(1);             (* 起线程，马上往下走 *)
    go download(2);             (* 和上一行并行 *)
    printf("main 继续\n");
```

* 三种写法的分工：

  | 写法 | 对函数的要求 | 引擎 | 返回 | 真并发 |
  |---|---|---|---|---|
  | `co f(a);` | 必须是 `async` | 无栈协程（帧拷贝） | 跑到第一个真正要等的地方 | 否 |
  | `task t = f(a);` | 初始化式是 `async` 调用 | 同上（保留句柄） | 同上 | 否 |
  | `go f(a);` | 任意普通函数 | `pthread_create` | **立刻** | **是** |

* 参数在 `go` 语句处**求值一次**，按值复制到堆上的参数块；线程入口
  （跳板）解开参数块再调用 `f`。之后改局部变量不影响线程。
* 能进参数块的类型：标量、枚举、指针、`T[:]`、文件作用域的
  struct/union。数组、VLA、函数指针参数（C38）、变参函数（C39）都不行。
* 被调函数不能是 `async`（用 `co`），也不能带 `!error`（C40）。
* 调用点能看到 `f` 的**文件作用域声明**就够了，定义可以在任何 TU。
  跳板按（本 TU，被调函数）生成，是 `static` 函数，在 TU 末尾定义；
  没有被 `go` 用到的原型不产生任何代码。
* 线程由**启动它的线程**回收：`main` 结束时编译器插入
  `__c6_th_join_all()`，别的线程在退出时回收自己的子线程。需要中途等待
  可以直接调用它。
* 每个线程有自己的错误槽、插值构造器和协程池（`pthread_key`，不是
  `__thread`：`-run` 不支持 TLS）。`defer` 不在这块状态里：清理状态就在
  函数帧里（见 8.2），所以线程里、ISR 里照常写；`err` / `f".."` / `async`
  才去取那块每线程状态，线程里也一样能用，还能再 `go`。

需要等某一个线程时用**句柄**（约束 C78）：

```
thread-declaration = "thread" , identifier , "=" , "go" , call , ";" ;
```

```c
thread h = go download(1);   (* 启动，这次调用的记录留在句柄里 *)
h->join();                   (* 阻塞到它结束；重复调用幂等，返回 0 *)
h->done();                   (* 1 = 已经结束，不阻塞 *)
h->value();                  (* 被调函数的返回值：整数或指针，否则别用 *)
```

句柄是启动者私有的：它只多持有一份线程记录（引用计数），线程仍然由启动者
回收，句柄作用域退出时把自己的那份还回去 —— 没 `join` 也不漏。线程自己
`join` 自己返回 `-1`，不会挂死。

运行时接口（`.c6` 的翻译单元里预置声明）：

```
void *__tcc_sp_args(unsigned long size);   (* 参数块 *)
int   __tcc_th_start(void *(*fn)(void *), void *arg);   (* 起线程，0 = 成功 *)
void *__tcc_th_spawn(void *(*fn)(void *), void *arg);   (* 起线程并返回句柄 *)
void  __c6_th_release(struct __c6_thread *h);           (* 句柄放手 *)
int   __c6_thread_join(struct __c6_thread *h);          (* h->join()  *)
int   __c6_thread_done(struct __c6_thread *h);          (* h->done()  *)
long long __c6_thread_value(struct __c6_thread *h);     (* h->value() *)
void  __c6_th_join_all(void);              (* 等本线程 go 出去的线程 *)
```

任务的句柄同样是方法（`struct NAME_task` 的第一个字段是 `struct __c6_task
head`，方法查找会回退到它的类型）：

```
task t = fetch(url);   t->wait();    (* 驱动事件循环直到它结束，0 / -1 *)
                       t->start();   (* 只跑到第一个挂起点（或跑完），不等 *)
                       t->done();    (* 1 = 结束，不驱动、不阻塞 *)
                       t->kind();    (* C6_CO_KIND_DONE / C6_CO_KIND_ERR *)
```

`task t = f(...)` **只创建任务**，body 要等 `t->wait()` / `t->start()`（或别人
`await` 它）才会跑；`t->start()` 是把一个任务推进到挂起态、然后让它挂着的唯一
办法（作用域结束时 `__c6_co_destroy` 会把它回卷：跑 `defer`、归还引用）。

`t->wait()` 会顺带推进**别的**顶层任务（它是 `__c6_co_wait`），而
`h->join()` 只等那一个 OS 线程：两个名字不通用。

### 3.7 tag 名直接作类型名（省略 `struct` / `union` / `enum`）

```
type-specifier         = "struct" , identifier
                       | "union"  , identifier
                       | "enum"   , identifier
                       | identifier ;          (* c6：可见的 tag 名 *)
```

tag 本身就是类型名，`struct` / `union` / `enum` 关键字可以省略（约束 C41）：

```
    struct Vec { int x, y; };
    struct Node { int v; Node *next; };   (* 定义体内就能省 *)
    union Word { int i; float f; };
    enum Color { RED = 1, GREEN = 2 };

    Vec v = { 1, 2 };             (* 变量定义 *)
    Vec *p = &v;                  (* 语句开头的 'Vec *p' 是声明 *)
    Vec vec_add(Vec a, Vec b);    (* 形参、返回值 *)
    struct Outer { Vec v; };      (* 结构体成员 *)
    int n = sizeof(Vec);          (* sizeof / alignof *)
    Vec *q = (Vec *)0;            (* 强转 *)
    Vec w = (Vec){ 3, 4 };        (* 复合字面量 *)
    Vec arr[2] = { {1,2}, {3,4} };
    Vec[:] vs = arr;              (* 切片元素 *)
    const Vec *cp = &v;           (* 限定符 *)
    typedef Vec VecAlias;         (* typedef 的目标 *)
    int vec_len(Vec *self);       (* 第一个形参是指针 => Vec 的方法 *)
```

* **普通命名空间优先**：同名变量、函数、枚举常量、typedef 在当前作用域可见时，
  按老 C 语义解析，裸 tag 不生效——需要类型就写 `struct`。典型例子是
  `<sys/stat.h>` 的 `struct stat` 与函数 `stat()`：`struct stat st;` 与
  `stat(path, &st)` 都照旧，`stat st;` 不行。
* 只接受**已经可见**的 tag，不会隐式前置声明：`S *p;` 在 `S` 没有声明时
  仍然报 `'S' undeclared`。
* 语句开头遇 `:` 时仍按标签解析（`TAG:` 是标签，不是类型）。
* `TAG` 只是 tag 时，语句开头的 `TAG(...)` 按声明解析（与 C 里 typedef 名
  的规则一致）；被函数遮蔽时按调用解析。同一条规则也意味着 K&R 定义里
  「形参名恰好与文件作用域 tag 同名」会按原型解析：
  `int f(S) int *S; { … }` 报 `';' expected`，写成 `struct S` 即可。
* 块作用域的 `TAG;` 只是引用外层 tag，不会像 `struct TAG;` 那样声明一个
  新的局部不完整 tag：本特性**不做任何隐式前置声明**。
* 只在 `.c6` 的翻译单元里生效；`struct Vec v;` 永远可用。

### 3.8 枚举成员写成 `Color.RED`

```
enumerator             = [ tag , "." ] , identifier ,
                         [ "=" , constant-expression ] ;
qualified-identifier   = identifier , "." , identifier ;   (* 枚举成员 *)
```

C 的枚举成员落在**普通标识符命名空间**里，所以两个枚举不能有同名成员：

```c
    enum Color { RED = 1 };        (* 同时引入标识符 RED *)
    enum Fruit { RED = 2 };        (* 错：redeclaration of 'RED' *)
```

c6 允许把成员写成 `<tag>.<成员>`，让每个枚举自带一层空间：

```c
    enum Color { Color.RED = 1, Color.GREEN = 2, Color.BLUE };
    enum Fruit { Fruit.RED = 10, Fruit.APPLE = 20 };

    Color.RED                (* 1 *)
    Fruit.RED                (* 10：两个 RED 各有各的值 *)
    BLUE                     (* 3：没被抢占的裸名照旧可用 *)
    int a[Color.BLUE];       (* 常量表达式里也能用 *)
    switch (c) { case Color.GREEN: … }

    int show(enum Color c) { return c == Color.RED; }
```

* 限定符必须是**这个枚举自己的 tag**（约束 C117）；匿名枚举没有 tag，成员
  不能限定（C118）。成员仍照旧注册成裸名，老代码 `RED` 不用改。
* 两个枚举都要同一个裸名时，裸名会被**毒化**（C119）：此后 `RED` 报错，
  必须写 `Color.RED` / `Fruit.RED`——它指谁不再取决于书写顺序。
* `Tag.名字` 是**表达式**，即使 `Tag` 同时也是类型名（§3.7）：`if`、`while`、
  `for` 的条件，`switch` 的 `case`，数组长度、位域宽度都能用；枚举里没有
  这个名字时按 C120 报错。
* 枚举必须已经定义并可见（与裸名同一条规则），不做任何隐式前置声明。
* 只在 `.c6` 的翻译单元里生效；纯 C 的翻译单元里没有裸 tag 名，
  `Color.RED` 报 `'Color' undeclared`（写成 `(Color)RED` 也只是强转）。

### 3.9 元组类型 `(T1, T2, ...)`

```
type-specifier         = "(" , type-name , "," , type-name ,
                         { "," , type-name } , ")" ;      (* c6：≥2 个元素 *)

tuple-literal          = "(" , type-name , "," , type-name ,
                         { "," , type-name } , ")" , initializer-list ;
tuple-element-access   = postfix-expression , "[" , integer-constant , "]" ;
```

元组是布局固定的值类型，用来把多个值一起传出去；元素用**常量下标**访问
（约束 C51–C60）：

```c
    (int, int) divmod(int a, int b) { return {a / b, a % b}; }
    (int, double) average(int a, int b) { return {(a + b) / 2, (a + b) / 2.0}; }

    (int q, int r) = divmod(17, 5);     (* 解构声明 *)
    (a, b) = divmod(100, 7);            (* 解构赋值 *)
    (int high, _) = divmod(100, 7);     (* '_' 丢弃这个元素 *)

    (int, int) t = {1, 2};
    t[0] = 9;                           (* 常量下标读写 *)
    int n = sizeof((int, int));         (* 8 *)
    (int, int) u = (int, int){3, 4};    (* 复合字面量 *)
    int z = ((int, int){5, 6})[1];      (* 6 *)

    struct Holder { (int, int) t; };    (* 结构体成员 *)
    (int, int) arr[4];                  (* 数组元素 *)
    ((int, int), int) nested;           (* 元素也可以是元组 *)
```

* 元组复用 `VT_STRUCT`：内部有一个 tag `__c6_tuple_N`，成员名 `__c6_t0`…
  只为实现细节。布局在**构造类型时**按自然对齐算好（
  `off = (off + align - 1) & -align`），与 `#pragma pack` 无关，因此同一个
  元组在任何 TU 都是同一个 ABI，并且与等价的 struct 完全一致——元组不引入
  任何新 ABI，参数和返回值都走已有的 struct 路径；
* 元素类型必须完整、非 `void`、非函数类型、非变长数组、非数组（C51），并且必须
  **在文件作用域可见**（C52，内部 tag 注册在全局作用域）；`error` 不能作
  元素（C53）；
* 至少两个元素（C54）。一元括号仍然只是普通括号：`(int)` 是强转，不是元组；
* 元组类型可以出现在任何类型位置：变量定义、形参、返回值、结构体成员、
  typedef、数组元素、强制转换、复合字面量、`sizeof` / `_Alignof`、切片元素；
* 括号表达式的原有语义**不受影响**：`(void)printf(…)`、`(int)x`、`(T)(x)`、
  `(struct S){…}`、语句表达式 `({ … })`、逗号表达式 `(a, b)` 全部按老 C
  解析——只有当括号里是一串“类型 [, 类型…]”且右括号前没有名字时才按元组
  类型解析（解析器做投机 + 回滚）。

### 3.10 函数字面量 `RET (参数) { 体 }`

```
function-literal       = type-specifier , [ "!error" ] ,
                         "(" , [ parameter-type-list ] , ")" ,
                         compound-statement ;
                         (* 值 = 函数指示符，退化成函数指针 *)

inferred-declaration   = identifier , "=" , function-literal , ";" ;
                         (* 类型从字面量推出：int(int x){…} 推出 int (*)(int) *)
```

函数字面量是一个**表达式**，它的值就是那个函数，和写一个函数名一样：
不分配对象、不做引用计数、运行期不携带任何额外信息（约束 C80–C83）。

```c
    void (*say)(int) = void(int x) { printf("%d\n", x); };
    say(1);

    void(int x) { printf("%d\n", x); }(2);      (* 就地调用 *)

    f = int(int x) { return x * 2; };            (* 推断声明 *)
    if (f(3) != 6) err(1, "…");

    static int (*table[2])(int) = {
        { int(int v) { return v + 1; } },
        { int(int v) { return v + 2; } },
    };
    qsort(v, n, sizeof v[0], int(const void *a, const void *b) {
        return *(const int *)a < *(const int *)b ? -1 : *(const int *)a > *(const int *)b;
    });
```

* 返回类型、参数表和函数定义完全相同：`!error`、变参、`T[:]`、元组、
  `@T`、struct 返回值都可以。字面量**没有名字**（C81），也不能带存储类
  或 `async`（C83）；
* 体是**稍后编译**的：和 `static inline` 一样，函数在翻译单元末尾发射，
  内部名字是 `__c6_fn_<序号>`（`static`），所以 `module` 不改写它，
  `-emit-header` 也不导出它。每个字面量各是一个独立的函数；
* 没有按值捕获时，体里只看得见**它自己的参数**和**文件作用域**的名字：只
  在 `sizeof`/`typeof` 里引用了外层局部、形参、块作用域的 `typedef`/tag 会报
  C80（裸函数指针带不了环境，也不能悄悄读一个已经返回的栈槽）。需要外层的
  值时把它作为参数传进来，或者把类型写成 `closure RET (P…)` 让字面量按值
  捕获（V2，见本节末尾）；
* 能写在任何可以写表达式的地方：文件作用域和 `static` 局部变量的初始化
  器、实参、返回值、结构体成员初值、数组初值、`static inline` 函数体里，
  以及另一个字面量的体里；
* 值就是普通的函数指针：C ABI 不变，可以存进任何同型函数指针、放进函数
  指针表、交给纯 C 保存和回调（`tests/abi/fn_lit.c6`），也可以直接传给
  `qsort` 这类标准库函数。

**V2 捕获式闭包（已落地）**：捕获式闭包引入类型构造

```
closure-type           = "closure" , type-specifier , [ "!error" ] ,
                         "(" , [ parameter-type-list ] , ")" ;
```

它的值是一个机器字的**托管引用**（隐藏对象 `{code, env}`），体里自由出现的
块作用域名字按值捕获进一个 `@struct` 环境；无捕获的字面量仍然完全按 V1
退化成裸函数指针。`closure` 是软关键字，只在类型位置生效：

```c
closure int(int) make_adder(int n)
{
    return int(int x) { return x + n; };   /* n 按值进 env */
}
```

捕获规则、转换表与分条消息见 [closure-design.md](closure-design.md)，验收
清单见 [closure-todo.md](closure-todo.md)；诊断编号 C90–C98 见下文约束表
（V1 的 C80 在 V2 下拆成 C93/C94：捕获了值的字面量只是一个闭包，要普通
函数指针就报 C93，捕获不能拷贝的东西报 C94）。

### 3.11 泛型（已落地）

类型应用是 `@` 后缀：`Vect@int`、`Map@(char[:], int)`、`Buf@(int, 4)`。
形参表在 tag 定义处挂名（名字先于体），在函数/typedef 处用 `|…|` 前缀
（返回类型或体先于名字，而形参必须先于使用）：

```
struct Vect@T        { T *data; int len; };
struct Map@(K, V)    { Vect@K keys; Vect@V vals; };
struct Buf@(T, int N) { T data[N]; int len; };      /* 值形参 */
struct Stack@(T, int N) {                            /* 体内成员函数 */
    T data[N]; int len;
    int cap(void) { return N; }
    int push(T v) { if (len >= N) return 0; data[len] = v; len = len + 1; return 1; }
    T pop(void) { len = len - 1; return data[len]; }
};
|T| T vat(Vect@T *self, int i);
|T| typedef struct { T v; } Box;

Vect@int a = {0};               /* 实例：struct Vect_int */
Vect@int *p = &a;
Buf@(int, 4) b = {0};           /* 值实参进 mangle：struct Buf_int_4 */
Vect@Vect@int grid = {0};       /* 右结合，链上没有闭合符 */
int x = p->vat(0);              /* 接收者定类型：Vect_int_vat */
Stack@(int, 4) s = {0};
int ok = (&s)->push(7);         /* 体内方法：Stack_int_4_push */
```

`@` 与托管引用共存：前缀 `@`（类型位置起点）是托管引用，后缀 `@`（类型名
之后）是类型应用，所以 `@Vect@int` 仍是「指向 `Vect@int` 的托管引用」。
尖括号不是泛型写法：`<` 在表达式里是合法运算符，单趟无 AST 无从消歧。

**已落地**：`struct Vect@T { … }` / `struct Map@(K, V) { … }` 模板声明，
`Vect@int` / `Map@(char[:], int)` 应用，嵌套（`Vect@Vect@int`）与自引用
（`Node@T` 里的 `Node@T *next`），`typedef` 别名，实例上的方法与切片后缀
（`Vect@int[:]` 是「实例的切片」）；函数/typedef 模板用 `|…|` 前缀绑定，
一份模板覆盖所有实例，`vat@int(v, 0)` 与 `p->vat(0)` 是同一个函数。实例
mangle 成 `Vect_int`，所以它就是一个普通 struct、普通函数：方法符号
`Vect_int_<name>`，实例函数 `Vect_int_vat`，纯 C 的 TU 写出 `struct
Vect_int` 即可互通。

**值形参**写成 C 参数表形状（`struct Buf@(T, int N)`），实参是常量表达式
（`Buf@(int, 4)`、`Buf@(char, N + 1)`），体内按普通常量绑定，所以能当数组
长度、`case` 标签、位域宽度；宽度不超过 `int`，负数在 mangle 里用 `m` 记
符号（`Buf@(int, -4)` → `struct Buf_int_m4`）。值实参**一律带括号**：常量
表达式不会像类型那样自己停下来，`Arr@4(x)` 会被读成 `4(x)`。

**实例名是普通 tag**：`module` 下和别的文件作用域名字一样带前缀
（`module m2;` 里 `Vect@int` 是 `struct m2__Vect_int`，方法 `m2__Vect_int_vat`），
`-emit-header` 因此直接写出 `struct m2__Vect_int` 和实例函数原型，跨 TU 一致。
用户手写同名 tag → `the instance name 'Vect_int' is already in use`。

**`-Wgeneric-depth`**（风格告警，默认关、`-Wall` 开）：源码里写出来的应用链
深于 2 层时提示起别名，`Vect@Vect@Vect@int` 报一次，`Vect@Vect@int` 不报，
同一个实例只报一次；模板体里「`A` 里嵌 `B`、`B` 里嵌 `Vect`」按写的 1 层算，
这种嵌套是正常用法，不告警。`-Werror` 下升级成错误，`-Wno-generic-depth`
关掉：

```
typedef Vect@int IntVect;      /* 起个别名，正文里就不写链 */
typedef Vect@IntVec Grid;
```

**体内成员函数**直接写在模板体里，接收者是隐式的 `self`：字段按名字读写
（`data[len]` 就是 `self->data[len]`），同一个模板里的方法按名字调用，先写的
方法可以调用后面才写的方法（原型先发、函数体留到单元末尾）；`!error`
方法、值形参 `N`、模板里再嵌实例照常可用。符号与 `|…|` 写法完全相同
（`Vect_int_at`），所以两种写法（以及纯 C 侧直接写 `Vect_int_at(p, i)`）
可以混用。模板体里只写原型（`T at(int i);`）是错误：模板体只解析一次，
体没有别处可补；非模板的结构体里写方法定义同样是错误（写出来就行
`int S_f(struct S *self) { … }`）。

裸模板名是 C103 诊断，不是类型。

完整语法、mangle/ABI、单态化方案、约束 C103–C116 与实施顺序见
[generics-design.md](generics-design.md)。

---

## 4. 语句

```
statement              = labeled-statement
                       | compound-statement
                       | expression-statement
                       | selection-statement
                       | iteration-statement
                       | jump-statement
                       | defer-statement
                       | errdefer-statement
                       | err-statement
                       | try-statement
                       | co-statement
                       | go-statement
                       | task-declaration
                       | foreach-statement
                       | tuple-destructuring ;

defer-statement        = "defer" , statement ;
                         (* 注册时跳过；离开作用域或返回/重抛时按注册的
                            逆序执行。注册 = 置本帧位图里的一位（幂等），
                            展开见 8.2 *)

errdefer-statement     = "errdefer" , statement ;
                         (* 只在错误路径上执行，行为同 defer *)

err-statement          = "err" , "(" , expression , "," , expression ,
                         ")" , ";" ;
                         (* 没有无参数形式：要转发错误请用 try *)

try-statement          = "try" , try-operand , ";" ;
                         (* 见第 5 节；作为语句时返回值被丢弃 *)

co-statement           = "co" , expression , ";" ;
go-statement           = "go" , expression , ";" ;
                         (* 顶层任务：建完立刻跑到第一个必须等待的点，
                            见 3.5 *)

task-declaration       = "task" , identifier , "=" , expression ,
                         { "," , identifier , "=" , expression } , ";" ;
                         (* 句柄在作用域退出时自动 __c6_co_destroy *)

foreach-statement      = "foreach" , binders , iterable , compound-statement ;

binders                = "|" , binder , [ "," , binder ] , "|" ;
binder                 = identifier | "_" ;
                         (* '_' 丢弃这个绑定 *)

iterable               = range | conditional-expression ;
range                  = conditional-expression , ":" , conditional-expression ;
                         (* 半开区间 [lo, hi)：两个界都只求值一次 *)

tuple-destructuring    = "(" , tuple-pattern , ")" , "=" ,
                         conditional-expression , ";" ;
tuple-pattern          = pattern-element , "," , pattern-element ,
                         { "," , pattern-element } ;
pattern-element        = type-specifier , declarator      (* 解构声明 *)
                       | "_"                              (* 丢弃这个元素 *)
                       | unary-expression ;               (* 解构赋值 *)
```

`tuple-destructuring` 只做**语句**形式，不产生表达式：右边必须是一个元组
值（C57），元素个数必须与模式一致（C55），模式要么全是“类型 + 名字”
（解构声明，新声明这些变量），要么全是左值或 `_`（解构赋值）。`_` 表示丢弃
对应元素，不建符号、不生成加载（C60）；混写两种形式会退回普通表达式解析，
因此会得到普通 C 的语法错误（C56）。

因为它是语句，所以可以出现在任何能写语句的地方：复合语句里、`if` / `while`
/ `do` / `for` / `switch` 的无花括号单条语句体里（`if (c) (int q, int r) = f();`）、
标签之后、语句表达式 `({ … })` 里，以及 `defer` / `errdefer` 的单条语句体里
（`defer (a, b) = f();`）。

`foreach` 的可迭代对象与绑定数量一一对应：

| 可迭代对象 | 绑定 | 下标类型 | 值绑定 |
|---|---|---|---|
| 完整数组 `E[N]`（含字符串字面量） | `\|i, v\|` | `size_t` | 元素的一份副本 |
| 切片 `E[:]` | `\|i, v\|` | `size_t` | 元素的一份副本 |
| 整数区间 `lo:hi` | `\|i\|` | 两个界的公共整数类型（至少 `int`） | 无 |
| 迭代器 `T` / `T *` / 接口值 | `\|v\|` | 无 | `next()` 返回值的副本 |

语义要点：

- 可迭代对象**只求值一次**：数组/切片落到一个隐藏的
  `{ E *data; size_t len }` 帧槽（与切片的运行期布局相同，所以
  `foreach |i, v| obj.items` 直接可用），区间的上界落到一个隐藏的整数槽，
  迭代器落到一个按它自身类型分配的隐藏槽；
- 数组每轮重新按 `data[i]` 取元素（数组可能在循环体里被改），切片用求值
  那一刻的 `{data, len}` 快照；
- 迭代器是**结构化协议**：类型提供 `int has(T *self)` 与 `E next(T *self)`，
  名字固定、不看接口叫什么。`T`（struct/union 值）直接调用，`T *` 直接
  调用，接口值通过 vtable 分派；两个方法只能有 receiver 一个形参，并且
  在 `foreach` 之前可见；
- `has()` 返回整数（含 `_Bool`/`enum`），非零才调用 `next()`；`E` 是
  `next()` 的声明返回类型，必须是完整、可赋值的对象类型（不能是 `void`、
  数组或函数；`void *` 因元素类型被擦除而拒绝）；
- 迭代器只有值绑定：`foreach |v| it { … }` 或 `foreach |_| it { … }`（后者仍调用
  `next()` 推进）。“值形态”把迭代器状态复制进隐藏槽，循环不推进原对象；
  `T *` 与接口值直接操作原对象（`next()` 的修改可见）；
- 迭代器和元素可以是托管引用 `@T`：隐藏槽按声明的托管局部量处理
  （先清零，再在 `foreach` 作用域结束时 `__c6_gc_release`），`@T` 形参
  按借用拷贝，不额外加计数；含托管字段的 `struct` 仍不能按值绑定
  （沿用 GC 的限制）；
- 值绑定是**副本**：写它不改变容器。下标绑定就是循环变量，写它会改变
  后续迭代（与 `for (i = 0; i < n; i++)` 完全一样）；
- `lo >= hi` 是零次迭代，两个界各只求值一次；
- `break`、`continue`、`defer`、`errdefer`、`await`、`goto` 与写在 `for`
  体里完全一致（循环体是普通复合语句，`break` 只跳出一层）；
- 循环体**必须**是复合语句（花括号不能省），绑定的作用域就是这条语句；
- 元素类型必须是**可赋值**的完整对象类型。元素本身是数组时（`int[2][3]`、
  `Row[:]`）无法按值复制——绑定会是一个数组，而数组在 C 里不可赋值——
  报约束 C49，改用下标区间：`foreach |i| 0:(int)rows.len { … rows[i] … }`；
- VLA、不完整数组、整数，以及既没有暴露切片、也没有 `has()`/`next()`
  协议的指针/结构体都不可迭代（见第 10 节）。迭代器的值绑定是
  `next()` 返回值的副本，`foreach |_| it` 合法但每一步仍调用 `next()`。

---

## 5. 表达式

```
primary-expression     = …
                       | qualified-identifier
                       | try-expression
                       | await-expression
                       | f-string ;                  (* 见 5.1 *)
                       | function-literal ;          (* 见 3.10 *)

f-string               = "f" , string-literal ;      (* const char *，借用的文本 *)
                       | "@" , "f" , string-literal ;
                         (* @char：把拼好的文本做成托管对象；见 C84/C85/R24 *)

await-expression       = "await" , unary-expression ;
                         (* 操作数最终必须落到一次 async 函数调用或
                            c6_io(fd, ev) 上，见 C30 *)

qualified-identifier   = identifier , "." , identifier ,
                         { "." , identifier } ;
                         (* 'pkg.member'：第一段必须是已 import 的包名，
                            否则按普通 C 的成员访问解析；见 C14/R7。
                            'Tag.MEMBER'（两段，`Tag` 是可见的 enum tag）
                            是枚举成员常量，见 3.8 与 C117–C120 *)

postfix-expression     = …
                       | postfix-expression , "->" , identifier ,
                         "(" , [ argument-expression-list ] , ")"
                         (* 方法调用：接收者须为指向结构体/联合体的指针，
                            且该类型有同名方法、没有同名字段；见 C20/R8 *)
                       | postfix-expression , subscript
                       | postfix-expression , slice-range
                       | postfix-expression , catch-clause ;

subscript              = "[" , expression , "]" ;
                         (* 基类型是切片时带边界检查并读取 .len；其它类型
                            保持 ISO C 的指针下标语义，见 R10 *)

slice-range            = "[" , [ expression ] , ":" , [ expression ] , "]" ;
                         (* 基类型是切片或定长数组；缺省 lo = 0、
                            缺省 hi = 切片/数组的长度，见 C25/R11 *)

                         (* 只有切片的 subscript 带边界检查：越界在运行时
                            陷阱。三个 ':' 的容量形式不支持，见 C26 *)

try-expression         = "try" , try-operand ;

try-operand            = call-expression ;
                         (* 语法上必须是一次函数调用，不接受任意一元表达式 *)

catch-clause           = "catch" , [ catch-binding ] , compound-statement ;

catch-binding          = identifier
                       | "(" , [ identifier ] , ")" ;
                         (* 只接受标识符，写别的记号会报 '{' expected *)

call-expression        = postfix-expression , "(" ,
                         [ argument-expression-list ] , ")" ;
```

结合性与优先级：`try` 是**前缀**运算符，它的操作数是一个
unary-expression，随后由约束 C2 要求该表达式的**根**必须是一次调用。
因此 `try f()->x` 的操作数是 `f()->x`（根不是调用）而**不合法**；要访问
调用结果，请用括号把 `try` 表达式括起来：

```
    (try f()).x        (* 合法：对 f() 的结果取字段 *)
    (try f())->x       (* 合法：f 返回指针时 *)
    try f()->x         (* 不合法：操作数是 f()->x *)
```

`catch` 作为**后缀**子句，紧跟在一次调用之后，作用在单个调用表达式上：

```
    v = f() catch e { … };          (* f() 是操作数 *)
    v = f(1) + g() catch e { … };   (* 只捕获 g() *)
    v = (f(1) + g()) catch e { … }; (* 语法错误：+ 不是调用 *)
```

`try` 与 `catch` 可以组合，先 `try` 后 `catch` 没有意义（`try` 已经把
错误传出去了），规范不禁止书写，但 `catch` 的操作数必须是**调用**而不是
`try` 表达式。

### 5.1 字符串插值 `f".."`

`f` 与后面的字符串字面量之间不能有空白。字面量的体被切成「文本片段」与
「插值」交替的序列：

```
    f"n={n} {{done}}"      (* 文本 "n="，插值 n，文本 " {done}" *)
    f"n={n;08x}"           (* 插值 n，格式说明 08x *)
```

每个插值可以带一个格式说明：

```
format-spec            = [ flags ] [ width ] [ "." precision ] [ conv ] ;
flags                  = { "-" | "0" | "+" | "#" } ;
width                  = digit , { digit } ;      (* <= 1023 *)
precision              = digit , { digit } ;      (* <= 1023 *)
conv                   = "d" | "i" | "u" | "x" | "X" | "o" | "b"
                       | "c" | "s" | "p"
                       | "f" | "F" | "e" | "E" | "g" | "G" ;
```

```
    f"[{s;10}]"      f"[{s;-10.3}]"      f"{n;08x}"      f"{ok;6}"
```

* `{` 与 `}` 之间是一个**表达式**（`}` 必须配对，见 C86）；`{{` / `}}` 输出
  字面的花括号，单个 `}` 是普通字符；
* 表达式里出现的引号要写成 `\"`，因为整个 f-string 仍是一个字符串字面量；
  其余转义（`\n`、`\t`、`\x41`、`\101`、`\\`）与普通字符串一致；
* 各段从左到右求值，每段只求值一次；
* 插值的类型必须是可格式化的（整数、`enum`、`_Bool`、`char`、指针、字符
  切片、托管 `@char`），见 C84；
* 按**静态类型**输出：`char` 是字符，字符常量 `'A'` 按 C 的规则是 `int`，
  插它得到 `65`（要字符就用 `char` 变量或写 `{(char)'A'}`）；`n > 0` 这类
  比较是 `int`，得到 `0`/`1`，`_Bool` 才得到 `false`/`true`；
* `f".."` 的类型是 `const char *`，`@f".."` 的类型是 `@char`（托管引用，
  见 8.1）；两者都不是常量表达式（C85）；
* 插值里不能用 `try`（C88）；`;` 只能出现一次，用来引入格式说明，语句
  表达式里的 `;` 因此也会被拒绝（C88）；宏形参在插值里不展开（表达式是宏
  展开之后才过预处理的），宏体、宏实参和 `#x` 字符串化都正常；
* 格式说明的语义：
  - flags：`-` 左对齐、`0` 补零、`+` 正整数也带符号、`#` 加进制前缀
    （`0x`/`0`/`0b`，值为 0 时不加，`p` 的 `0x` 不受影响）；
  - width：最少列数，不够用空格（`0` 时用零）补齐；`-` 时补在右边；
  - `.precision`：整数是最少位数（前面补零），文本是最大字节数；
  - conv：不写就按静态类型（见上表）；写了就按它——`u`/`x`/`X`/`o`/`b` 把
    值当**无符号**解释（`f"{-5;x}"` 是 `fffffffffffffffb`），`d`/`i` 保留
    符号，`c` 取低字节当字符，`s` 把值当文本，`p` 当指针（默认带 `0x`）；
  - 能用的 conv 由静态类型决定（C89）：整数/`enum`/`_Bool`/`char` 接受
    `d i u x X o b c s`（`_Bool` 用 `s` 时是 `true`/`false`），文本只接受
    `s`，指针只接受 `p x X`，浮点只接受 `f F e E g G`；
  - 浮点：默认是 `g`；`f` 定点（默认 6 位小数）、`e` 科学计数法
    （`1.234568e+06`）、`g` 取两者里较短的那种并去掉结尾的零；
    `.precision` 对 `f`/`e` 是小数位数、对 `g` 是有效数字位数；数字由运行时
    自己算（libtcc1 没有 libc），常见范围精确，需要全部 17 位有效数字时最后
    一位可能不同；舍入是四舍五入（不是 printf 的四舍六入五成双）；
    `|v| >= 1e19` 用科学计数法；`long double` 按 `double` 打印；
* `f".."` 的结果是**借用**：同一个嵌套深度只有一块结果缓冲，同一个完整表达式
  里的两个 `f".."` 会互相覆盖；要同时活着就写 `@f".."`。

---

## 6. 预处理

```
c6-directive      = "#" , "macro" , identifier ,
                      [ "(" , [ parameter-list ] , ")" ] ,
                      "{" , { preprocessing-token } , "}" ;

parameter-list    = identifier , { "," , identifier } ;
```

`#macro` 与 `#define` 的区别：

- 宏体用 `{ … }` 而不是“到行尾”，因此可以跨行、可以包含分号；
- 形参表中的标识符在替换前先做替换（不需要 `#` 前缀）；
- 宏体内以 `__local` 声明的标识符被**卫生重命名**为 `ident__hN`
  （`N` 是展开计数器），因此不会与调用点的同名标识符冲突：

```
    #macro swap(a, b) {
        typeof(a) __local t = (a);
        (a) = (b); (b) = t;
    }
```

---

## 7. 静态约束

下列约束由编译器检查，违反时给出对应的诊断信息。

| 编号 | 约束 | 诊断 |
|---|---|---|
| C1 | `try` 只能出现在返回类型标记了 `!error` 的函数体内 | `'try' requires the enclosing function to return 'T!error'` |
| C2 | `try` 的操作数必须是调用表达式 | `'try' requires a call to a function returning 'T!error'` |
| C3 | `try` 的被调函数返回类型必须标记了 `!error` | 同 C2 |
| C4 | `catch` 的操作数必须是调用表达式 | `'catch' requires a call to a function returning 'T!error'` |
| C5 | `catch` 的被调函数返回类型必须标记了 `!error` | 同 C4 |
| C6 | `catch` 的处理块必须自己离开（`return` / `err(id, msg)` / `goto`） | `'catch' body must end with 'return', 'err()' or 'goto'`；静态判断不出来时改为插入运行时陷阱 |
| C7 | `catch` 必须带操作数 | `'catch' has no operand` |
| C8 | `!error` 必须修饰函数（或函数指针）声明符 | `'!error' requires a function declarator` |
| C9 | `!error` 的函数返回类型必须是标量、枚举、指针、struct、union 或元组：标量失败时返回 `-1`，聚合类型失败时清零 | `'!error' does not support a 'double' return type` |
| C10 | `expr` 中不能直接写裸 `catch` | 同 C7 |
| C11 | `try` / `catch` / `err` 不得出现在 `defer` / `errdefer` 体内 | `'try' is not allowed in a defer body` 等 |
| C12 | 返回类型大小未知（不完整类型）的函数不能传播错误 | `cannot propagate an error from a function with an incomplete return type` |
| C13 | `package` 内的符号被改写为 `NAME__sym`，`main` 除外；嵌套与点号形式组合前缀 | — |
| C14 | `pkg.member` 只在 `pkg` 已 `import`（或就是当前包）时成立 | `'pkg' is not an imported package (use 'import pkg;')` |
| C15 | `T!error` 调用的结果必须被 `try` 或 `catch` 消费（`sizeof` / `typeof` 的操作数不产生调用，不受此限） | `the result of a 'T!error' function must be used with 'try' or 'catch'` |
| C16 | `err` 必须带 `id` 与 `msg` 两个实参（没有重抛形式） | `'err' requires an error id and a message` |
| C17 | `err` 只能出现在返回 `T!error` 的函数里 | `'err' requires the enclosing function to return 'T!error'` |
| C18 | `error` 只能作为返回值，不能作为形参（含 `error*` / `error[]`，K&R 形参声明也算） | `'error' can only be a return type, not a parameter` |
| C19 | 每个变量定义都必须带初始化器（`extern` 声明、形参、成员、`typedef`、`static` 变量除外；VLA 豁免） | `variable 'x' must be initialized` |
| C20 | 方法调用要求：接收者是指向结构体/联合体的指针、该类型有同名方法、没有同名字段、方法在调用点之前已声明，且方法后面必须紧跟 `(` | `method 'f' must be called`；`duplicate method 'f' for 'T'`；其它情况按普通成员访问报 `field not found: f` |
| C21 | 结构体到接口的转换要求接口的每个方法都在**转换点之前**可见且签名兼容 | `'T' does not implement interface 'I': missing method 'm'`；`method 'm' of 'I' has an incompatible return type` / `… parameter` / `… wrong number of parameters` |
| C22 | `[:]` 的元素必须是完整对象类型（不能是 `void`、函数、不完整类型或 VLA），且不能是 `error` | `'[:]' cannot have a 'void' element type`；`'[:]' requires a complete element type, not 'T'`；`'[:]' cannot have a function element type`；`'[:]' cannot have a variable length array element`；`'error' can only be a return type, not a slice element` |
| C23 | 切片的 struct/union/enum 元素必须在文件作用域声明（函数内的局部 tag 会随块退出释放） | `the element type of '[:]' must be declared at file scope, but 'T' is local` |
| C24 | 数组到切片的隐式转换只接受**定长**数组，且元素类型必须一致 | `cannot make a slice of 'E' from an array of 'E2'`；`cannot slice a variable length array`；`cannot slice an array of unknown length` |
| C25 | 只做区间 `[lo:hi]` 的对象必须是切片或定长数组；指针不行 | `cannot slice a pointer; use '(T[:]){p, n}'` |
| C26 | 区间没有容量形式（第三个 `:`） | `slice bounds have no capacity` |
| C27 | 静态初始化器（文件作用域或 `static` 局部）没有运行时代码，数组不能隐式转切片 | `a slice cannot be initialized from an array in a static initializer; use '{ data, len }'` |
| C28 | 切片的元素限定符按忽略处理（`const int[:]` 与 `int[:]` 是同一类型） | — |
| C29 | `async` 只能用于文件作用域的函数定义（不能嵌套、不能 `inline`、不能是 `main`），且签名必须是可写回的类型：标量、指针、`T[:]`、文件作用域的 struct/union，不能有数组、VLA 或函数指针参数 | `'async' functions must have file scope`；`'async' functions cannot be nested`；`'async' cannot be combined with 'inline'`；`'main' cannot be async`；`'async' does not support this signature: '…'` |
| C30 | `await` 只能出现在 `async` 函数体内，操作数必须是一次 `async` 调用或 `c6_io(fd, ev)` | `'await' is only allowed inside an 'async' function`；`'await' requires a call to an 'async' function` |
| C31 | `await` 不能出现在 `defer` / `errdefer` 体或 `sizeof` 等不求值上下文里 | `'await' is not allowed in a 'defer' body`；`'await' is not allowed in an unevaluated context` |
| C32 | `c6_io` 只有 `await c6_io(fd, ev)` 一种写法 | `'c6_io' can only be used as 'await c6_io(fd, ev)'` |
| C33 | 挂起点上已经压在求值栈里的中间值必须落在变量里（帧槽会随帧一起恢复，寄存器不会） | `the value pending across 'await' must live in a variable: store it in a local first` |
| C34 | `co` / `go` / `task` 只能出现在函数体内 | `'co' can only be used inside a function`；`'go' can only be used inside a function`；`'task' can only be used inside a function` |
| C35 | `co` 的操作数必须是一次 `async` 调用 | `the operand of 'co' must be the handle of an 'async' call`；`'co' requires an 'async' function: 'f' is a plain function (use 'go')` |
| C36 | `task` 的初始化器必须是一次 `async` 调用（类型从初始化器推出，所以不能省） | `the initializer of a 'task' variable must be a call of an 'async' function`；`'=' expected` |
| C37 | `async` 函数不能是方法（方法调用会被改写成普通调用，指向不存在的函数体） | `'async' functions cannot be methods: '…'` |
| C38 | `go` 的操作数必须是**普通函数**的调用：不能是 `async` 函数（用 `co`）、不能是函数指针、参数类型必须能写进参数块（数组/VLA/函数指针形参不行） | `the operand of 'go' must be a call of a plain function`；`'go' requires a plain function: 'f' is 'async' (use 'co')`；`'go' does not support this signature: 'f'` |
| C39 | `go` 不支持变参函数（参数块要按声明的形参布局） | `'go' does not support variadic functions: 'f'` |
| C40 | `go` 不支持 `!error` 函数（错误没人接） | `'go' does not support fallible functions: 'f'` |
| C41 | 裸 tag 只有在该 tag 可见、且同名**普通符号**（变量/函数/枚举常量/typedef）不在作用域内时才当作类型名；只接受已声明的 tag，不隐式前置声明；语句开头的 `TAG:` 仍是标签；K&R 定义里形参名与 tag 同名时按原型解析 | —（遮蔽/未声明时按普通 C 解析：`';' expected`、`'X' undeclared`、`initialization of incomplete type`） |
| C42 | `foreach` 只能出现在函数体内 | `'foreach' can only be used inside a function` |
| C43 | 绑定表是 1 或 2 个标识符（`_` 表示丢弃） | `'foreach' expects a binding name`；`'foreach' takes at most two bindings` |
| C44 | 数组/切片要两个绑定 | `an array or slice yields an index and a value: write 'foreach \|i, v\| a { ... }'` |
| C45 | 区间只要一个绑定 | `a range yields only an index: write 'foreach \|i\| lo:hi { ... }'` |
| C46 | 两个绑定不能都丢弃 | `'foreach' discards both binders` |
| C47 | 可迭代对象只能是完整数组、切片、整数区间，或提供 `has()`/`next()` 的迭代器 | `cannot iterate over a variable length array`；`cannot iterate over an array of unknown length`；`cannot iterate over 'T *' (no 'has'/'next' iterator: use a slice parameter 'T[:]', or '(T[:]){p, n}')`；`cannot iterate over 'int' (write a range: 'foreach \|i\| lo:hi { ... }')`；`cannot iterate over 'T' (no 'has'/'next' iterator: expose a slice member 'T[:] items', or define 'int has(T *); E next(T *);')` |
| C48 | 区间两个界必须是整数 | `range bounds must be integers` |
| C49 | 元素类型必须可赋值且完整 | `'foreach' cannot copy an element of type 'T'`；`cannot iterate over incomplete element type 'T'` |
| C50 | 循环体必须是复合语句（数组/切片、区间、迭代器三种形态共用这一处检查；C44/C45/C100 的提示里那个 `{ ... }` 就是这条要求） | `'{' expected` |
| C51 | 元组元素类型必须完整、非 `void`、非函数类型、非变长数组、非数组 | `'tuple' element cannot be 'void'`；`'tuple' element cannot be a function type`；`'tuple' element cannot be a variable length array`；`'tuple' element cannot be an array type (use a pointer, a slice 'T[:]' or a struct)`；`'tuple' requires a complete element type, not 'T'` |
| C52 | 元素类型的 tag 必须在文件作用域 | `the element type of a tuple must be declared at file scope, but 'T' is local` |
| C53 | `error` 不能作元组元素 | `'error' can only be a return type, not a tuple element` |
| C54 | 元组至少两个元素（`(T)` 仍是普通括号表达式） | — |
| C55 | 解构的元素个数必须与元组一致 | `the tuple has %d elements but the pattern has %d` |
| C56 | 解构模式要么全是“类型 + 名字”，要么全是左值 / `_`，不能混写 | 退回普通表达式解析：`')' expected`、`expression expected before …`、`lvalue expected` |
| C57 | 解构右边必须是元组 | `destructuring requires a tuple value` |
| C58 | 元素下标必须是整数常量且在 `[0, n)` 内 | `constant expression expected`；`tuple index 2 is out of range (0..1)` |
| C59 | `return { … };` 只能出现在返回类型为 struct / union / 元组的函数里 | `'return {…}' requires a struct, union or tuple return type` |
| C60 | 解构不能把所有元素都丢弃 | `'tuple' discards every element` |
| C61 | `module NAME;` 只能出现在文件的第一个声明之前，一个文件只能有一个 | `'module' must be the first declaration in the file`；`module 'm' already declared in this file` |
| C62 | 模块里不能定义 `main` | `'main' cannot be declared in a module` |
| C63 | `import` 打开的文件必须声明同名模块（点号形式按组合后的前缀比较） | `module file declares 'a__b', expected 'a__c'` |
| C64 | 模块不能直接或间接 import 自己 | `circular import of module 'x'` |
| C65 | `-emit-header` 只能用于声明了 `module` 的翻译单元；匿名 struct/union 必须已有 typedef 才能写出 | `-emit-header requires a 'module NAME;' declaration`；`cannot write a C header for an anonymous struct type; give it a typedef first` |
| C66 | 强引用类型图里不能有环（含自环、数组、按值嵌入） | `reference cycle: A -b-> B -a-> A; break it with 'weak' or 'own' (type 'A' at a.c:3)` |
| C67 | 只有"帧拷贝"后端（c67）上 `weak` 才不能出现在 `async` 函数里；`-b` 不能与 `async` 同用 | `a 'weak' reference cannot be declared in an async function: its frame is moved to the task`；`'-b' cannot be combined with 'async'` |
| C68 | 含托管字段的结构体不能按值拷贝/传参/返回 | `'%s' holds managed references: %s is not possible. Allocate it with 'new' and hold it in '@%s'` |
| C69 | 托管引用只能存进托管槽；借出的指针不能保存 | `a new reference cannot be stored in '%s': it is not a managed reference`；`storing a borrowed reference keeps it alive without counting` |
| C70 | 形参是借用，不能改写 | `'%s' is a borrow: it owns nothing, so it cannot be reassigned (copy it into an owned local first)` |
| C71 | `own @T` 只接受 `new` 的结果、`move()` 或空值 | `'%s' takes ownership: only a fresh reference ('new') or the empty value can be stored in it` |
| C72 | `shared @T` 槽要求对象用 `new shared T` 建（计数在对象头里） | `a 'shared' slot needs an object with an atomic count: create it with 'new shared T', not 'new T'` |
| C73 | `@T`/`own @T` 不能跨线程或跨帧（`go`/`async` 只收 `shared @T`） | `passing a reference to 'go' requires 'shared'`；`passing a reference to 'async' requires 'shared'` |
| C74 | `union` 不能含托管字段；含托管字段的结构体必须在文件作用域定义、不能有托管柔性数组 | `'%s' is a union with a managed field: ...`；`... is defined in a block: define it at file scope`；`... has a flexible array of managed references` |
| C75 | `memcpy`/`memmove` 不能作用于含托管字段的对象；`memset` 会漏引用（告警） | `... breaks reference counting; use a typed copy or 'own'` |
| C76 | 析构 `drop` 不能手动调用、不能失败/挂起、必须返回 `void`、每个类型至多一个 | `a destructor cannot be called: ...`；`a destructor cannot fail: ...`；`a destructor cannot suspend: ...`；`'%s' already has a destructor` |
| C77 | `move(x)` 的操作数必须是槽里的托管引用 | `'move' needs a managed reference held in a slot, not a '%s'` |
| C78 | `thread` 句柄的初始化式必须是 `go` 调用；句柄不能跨线程传、不能手动 `free` | `the initializer of a 'thread' variable must be 'go f(...)'` |
| C79 | `move(p)` 之后、重新赋值之前不能再用 `p`（读/比较/解引用/传参/再 `move`） | `'%s' was moved: assign a new reference before using it` |
| C80 | 字面量里引用了外层块作用域的名字，但它**没有按值捕获**（只在 `sizeof`/`typeof` 这类不求值的位置用到，或只是类型别名），于是它仍然退化成裸函数指针——裸函数指针带不了环境。有按值捕获时它就是一个闭包：放进 `closure RET (P…)` 槽（或推断声明）即可；要把它当普通函数指针用报 C93，捕获的东西不能按值拷贝报 C94 | `a function literal cannot capture 'x': without captures it is a plain function pointer, so its body sees only its parameters and the file scope (pass the value in as a parameter)` |
| C81 | 函数字面量没有名字；名字要写在声明符里 | `a function literal has no name: 'p'` |
| C82 | 函数字面量必须有完整的返回类型、参数表和函数体 | `a function literal needs a full signature: 'RET (parameters) { body }'`；`'{' expected: the body of the function literal`；`a function literal needs a typed parameter list` |
| C83 | 函数字面量不能带存储类，不能是 `async` | `a function literal cannot have a storage class`；`a function literal cannot be 'async'` |
| C84 | f-string 的插值只接受整数、`enum`、`_Bool`、`char`、浮点、指针、字符切片和托管 `@char`；切片必须是字符切片 | `cannot interpolate a value of type 'struct S'`；`cannot interpolate 'int[:]': only a character slice holds text` |
| C85 | `f".."` / `@f".."` 不是常量表达式，不能出现在文件作用域或 `static` 局部的初始化式里 | `an f-string is not a constant expression` |
| C86 | f-string 里的 `{` 必须有配对的 `}`，且 `{}` 里要有一个表达式 | `f-string: missing '}' in ".."`；`f-string: '{}' needs an expression` |
| C87 | `async` 函数体里不能声明变长数组、不能调 `alloca`（栈上的空间挂起之后不归这个 body） | `a variable length array cannot be declared in an 'async' function: it would not survive a suspension`；`'alloca' cannot be used in an 'async' function: it would not survive a suspension` |
| C88 | 插值里不能用 `try`（错误路径会直接返回，运行时的构造器留在栈上）；`;` 只能出现一次（引入格式说明），语句表达式里的 `;` 同样被拒绝 | `'try' is not allowed in an interpolation: ...`；`f-string: ';' is only allowed once, to start the format spec ('{expr;spec}')` |
| C89 | 格式说明要与插值的静态类型相容，`conv` 必须是已知的，宽度/精度不超过 1023，`;` 后面不能为空 | `f-string: unknown conversion 'z' in a format spec`；`f-string: a 'const char *' value takes no 'x' format spec`；`f-string: format width 2000 is too large`；`f-string: empty format spec in '{expr;spec}'` |
| C90 | `closure` 后必须是**完整**函数签名；签名里不能出现匿名 `struct`/`union`/`enum`、块作用域里声明的类型或 `async` 函数；闭包本身已经是托管引用，不能再套 `@` | `a 'closure' type needs a full signature: 'closure RET (parameters)'`；`a closure signature cannot use an anonymous 'struct'/'union'/'enum': give it a tag`；`a closure signature cannot use a type declared in a block`；`a 'closure' type cannot wrap an 'async' function`；`'@' cannot be applied to a closure type: a closure is already a managed reference` |
| C91 | 闭包不能变成普通函数指针，也不能变成 `void *` 等普通指针：它是 `{code, env}` 的不透明托管对象 | `a closure cannot become a plain function pointer: it carries an environment (call it, or keep it as 'closure RET (parameters)')`；`a closure cannot be converted to '%s': closures are opaque` |
| C92 | 变参函数指针（或变参的非捕获字面量）不能包装成闭包：跳板转发不了未知的实参表；要变参就写成捕获式字面量 | `a variadic function pointer cannot be wrapped into a closure: a trampoline cannot forward an unknown argument list (make it a capturing literal instead)` |
| C93 | 有按值捕获的字面量出现在需要普通函数指针的位置（实参、返回值、函数指针变量），它已经是一个闭包；V1 的 C80 文本会在它之前照旧打印一遍 | `this literal captures '%s', so its value is a closure, not a function pointer: use 'closure RET (parameters)' as the type, or pass the value in as a parameter` |
| C94 | 捕获了不能按值拷贝的名字/类型：`own`/`weak` 引用、块作用域 `static`/函数/枚举常量/类型/tag/枚举、数组、VLA、方法接收者、含 `drop` 或托管字段的结构体、含托管元素的元组、柔性数组、thread/task 句柄 | `a closure cannot capture '%s': an 'own' reference cannot be copied`；`a closure cannot capture '%s': upgrade the 'weak' reference to '@T' first`；`a closure cannot capture the block-scope 'static' '%s': move it to file scope`；`a closure cannot capture the function '%s' declared in a block: move the declaration to file scope`；`a closure cannot capture the constant '%s': move it to file scope`；`a closure cannot capture the array '%s': pass a slice 'T[:]' or a pointer instead`；`a closure cannot capture the variable length array '%s': its size is only known at run time`；`a closure cannot capture '%s': its type is declared in a block (move the type to file scope)`；`a closure cannot capture the method receiver '%s': ...`；`a closure cannot capture '%s': the type has a 'drop' destructor, ...`；`a closure cannot capture '%s': it holds managed references ...`；`a closure cannot capture '%s': a thread/task handle owns a resource ...`（分条消息全文见 [closure-design.md](closure-design.md) §9） |
| C95 | 文件作用域或 `static` 局部的初始化式里不能放非空闭包（要分配）；`NULL` 可以，运行时再赋值 | `a closure is not a constant expression: initialize it at run time ('NULL' is fine at file scope)` |
| C96 | 闭包不能带 `shared`/`weak`/`own`，也不能跨线程或任务（`go`/`async`/`thread` 的载荷、`async` 的参数与返回值） | `a closure cannot be 'shared', 'weak' or 'own': V2 closures are confined to the thread that creates them`；`a closure cannot cross a thread or a task: V2 closures are confined to the thread that creates them` |
| C97 | 对闭包做不透明操作：`->`、`*`、下标、算术、`new`、`offsetof`（只能调用、和 `NULL` 比较、拷贝、`move`、置空释放） | `'%s' is a closure: it can only be called, compared with NULL, copied, moved or released`；`a closure cannot be created with 'new': use a function literal or a function pointer` |
| C98 | 直接赋给自己的字面量捕获了目标槽自己（闭包会看到旧值；递归要写成具名函数） | `'%s' is captured by the closure being assigned to it: the closure would see the old value (use a named function for recursion)` |
| C99 | 迭代器方法签名：`has()` 返回整数、`next()` 返回完整可赋值对象类型（`void *` 会擦除元素类型）、两个方法只接收 receiver | `iterator method 'has' must return an integer type (got 'T')`；`iterator method 'next' must return a value (got 'void')`；`iterator method 'next' must return an object type (got 'T')`；`iterator method 'next' cannot return 'void *': the element type would be erased (return 'E' or 'E *' with a concrete E)`；`iterator method 'has' must take only the receiver`；`iterator method 'next' must take only the receiver`；`iterator method 'next' cannot return a borrowed managed reference 'T'` |
| C100 | 迭代器只产生一个值绑定 | `an iterator yields only a value: write 'foreach \|v\| it { ... }'` |
| C101 | 一个函数最多 128 个清理点（`defer` / `errdefer` / 编译器合成的 site） | `a function cannot have more than 128 cleanup sites: split it into smaller functions` |
| C102 | 清理体最多嵌套 8 层（清理体里再退出作用域会形成一层新的派发），第 9 层报错 | `'defer' bodies nested too deeply` |
| C103 | 裸模板名不是类型：模板名只能出现在 `@` 实参表前面 | `'%s' is generic: write '%s@%s'` |
| C104 | 实参个数必须等于形参个数，多形参要用括号 | `'%s' needs %d type argument%s (got %d): write '%s@(A, B)'` |
| C105 | 实参必须是类型；实参里不能写声明符名字；形参只在模板体内可见，用在使用点之前要写 `\|…\|` 前缀 | `a type argument is expected`；`a type argument cannot declare '%s'`；`'%s' undeclared (a type parameter is only visible inside its template)`；`type parameter '%s' is used before it is declared: write '\|%s\| %s %s(...)'` |
| C107 | 模板体内不能实例化自己（唯一会自噬的递归源） | `cannot instantiate '%s' inside its own definition` |
| C108 | 实例化点必须在模板定义之后，前向声明的模板不能直接实例化 | `instantiation of undefined template '%s'` |
| C110 | 值形参的实参必须是常量表达式、能装进形参类型；形参宽度不超过 `int` | `constant expression expected`；`value argument %lld does not fit in '%s'`；`a value parameter must be an integer type no wider than 'int'`；`a value parameter is written '<type> NAME'` |
| C111 | 每个 TU 最多 1024 个实例；实例名是保留的 ABI 名，不能被手写 tag 占用 | `too many generic instantiations in one translation unit (max 1024)`；`the instance name '%s' is already in use: rename the struct` |
| C112 | 实例化嵌套最多 64 层 | `generic instantiation nested too deeply (max 64)` |
| C113 | `enum` 不能带形参 | `enums cannot have type parameters` |
| C114 | 实例名前面不能写 `struct`：`struct Vect@int v;` 不是实例 | `an instance drops 'struct': write '%s@...'` |
| C115 | 模板不能重定义，参数表也不能改；`struct`/`union` 不能混用；形参不能重名，也不能与模板同名 | `redefinition of template '%s'`；`redefinition of template '%s' with a different parameter list`；`redefinition of template '%s' as a different kind`；`duplicate type parameter '%s'`；`a type parameter cannot have the same name as its template` |
| C116 | 形参最多 16 个（括号里或单个） | `too many type parameters`；`too many type arguments` |
| C117 | 枚举成员的限定符必须是这个枚举自己的 tag | `enumerator qualifier 'Other' must be the enum's own tag 'Color'` |
| C118 | 匿名枚举的成员不能限定 | `the enumerator of an anonymous enum cannot be qualified` |
| C119 | 两个枚举都要同一个裸成员名时，裸名不再可用，必须写限定名 | `'RED' is the member of more than one enum: write '<enum>.RED'` |
| C120 | `Tag.成员` 里 `Tag` 必须是可见的枚举，成员必须存在 | `'Fruit' has no member 'APPLE'`；`'Nope' is neither an enum nor an imported package (use 'import Nope;')` |

（闭包的 C90–C98 见 [closure-design.md](closure-design.md) §9，消息与
`tccgen.c` 同步落地；C80 在 V2 下只剩「没有按值捕获、却引用了块作用域名字」
这一种情况。
枚举成员限定的 C117–C120 见 §3.8。
泛型的 C103–C116 已全部落地：C106 与 C104 共用一条消息（多参数必须加括号），
C109 由普通类型检查给出（`cannot convert 'struct Vect_char' to 'struct Vect_int'`），
见 [generics-design.md](generics-design.md) §9。）

---

## 8. 语义重写规则

扩展的语义可以用下列重写规则描述；规则中的 `$T` 表示被调函数的返回
类型，`$S(x)` 表示“把 x 转成返回类型后可表示的哨兵值 `-1`”。

**R1 — `err(id, msg)`**

```
err(id, msg);
=>
    __tcc_err_set(id, msg);        (* 写入错误槽：flag=1, id, msg *)
    <本函数帧的 errdefer 与 defer，按注册逆序就地展开>
    return $S(T);                  (* 函数返回类型为 void 时只 return *)
```

**R2 — 强制消费**：`T!error` 调用的结果只能被 `try` 或 `catch` 消费。
赋值、参与运算、作条件、`return` 出去或当语句丢弃都属于约束 C15
（`the result of a 'T!error' function must be used with 'try' or 'catch'`），
保证错误不会被静默忽略。

**R3 — 每次 `T!error` 调用都从干净的错误槽开始**：调用之前先执行
`__tcc_err_clear()`，因此调用之后的错误槽只反映这一次调用。这样
`catch` 不会把之前遗留的错误误判成这次调用的失败，`try` 之后槽里也
必然是空的。

**R4 — `try f(args)`**

```
try f(args)
=>
    __tcc_err_clear();             (* 由调用点插入，见 R3 *)
    $T tmp = f(args);              (* 记下返回值，失败时哨兵即 -1 *)
    if (__tcc_err_flag_get()) {
        <本函数帧的 errdefer 与 defer，按注册逆序就地展开>
        return $S(T);              (* 函数返回类型为 void 时只 return *)
    }
    tmp
```

即 `try` 是「调用 + 检查错误槽 + 失败则原样传出」。

**R5 — `expr catch e { body }`**

```
expr catch e { body }
=>
    $T tmp = expr;                 (* expr 必须是一次调用，调用前清槽 *)
    if (__tcc_err_flag_get()) {
        error e = __tcc_error_take();   (* 快照 id/msg，并清空错误槽 *)
        body;                           (* body 必须自己离开 *)
        unreachable();
    }
    tmp
```

绑定形式 `catch { … }` 与 `catch (e) { … }` 等价于不绑定变量或绑定变量，
`catch () { … }` 等价于 `catch { … }`。处理块必须先离开；当编译器无法
静态证明（例如 `if (c) return 1;` 没有 else）时，仍在处理块末尾插入
`__tcc_catch_fallthrough()` 作为陷阱，避免悄悄落回成功路径。

**R6 — `T!error f(…)`**：只改变调用点的类型信息（允许 `try` / `catch`
作用其上），不改变调用约定；错误只通过返回值送出（失败方返回哨兵 `-1`，
id/msg 走错误槽），从不经过形参，因此 `error` 被禁止出现在形参表里
（约束 C18）。

**R7 — 包限定访问 `pkg.member`**

```
pkg.member                       (* 'pkg' 已 import，或就是当前包 *)
=>
    pkg__member                  (* 与原符号是同一个普通 C 符号 *)
```

点号逐段增长：`a.b.f` 先试 `a__b`，再试 `a__b__f`；命中后剩下的
`.field` 交回普通成员访问，所以 `pkg.obj.field` 可用。第一段若命中
当前可见的普通标识符，则**不**做包解析（保持 C 的成员访问语义）。

**R8 — 方法调用 `p->f(args)`**

```
p->f(args)                       (* T 有方法 f，且没有同名字段 *)
=>
    T_f(p, args)                 (* 符号按接收者取名，调用约定不变 *)
```

接收者先求值并存入一个临时槽，再作为第一个实参压栈：若方法返回结构体，
隐藏的 `self` 排在 sret 缓冲之后。`T_f` 就是方法定义处输出的符号
（见 3.2；`static`/`inline`、`__` 开头、系统头文件里的声明和
`package` 内的方法保留原名）。

**R9 — 结构体到接口的隐式转换**

```
Shape s = circle;                (* 结构体值 -> 接口值，C21 通过 *)
=>
    static const struct Shape_vt __tcc_vt_Circle_Shape = {
        (void (*)(void))area, (void (*)(void))name
    };
    __tcc_iface_init(&circle, &__tcc_vt_Circle_Shape, &s);
```

vtable 对象在**翻译单元末尾**按用到的 (类型, 接口) 对生成，每个 TU 一份
`static`，因此多个 TU 转换同一类型不会符号冲突。文件作用域的初始化器没有
可执行代码，因此不在支持范围（见第 10 节）。

---

**R10 — 切片下标 `s[i]`**

```
s[i]                             (* s 的类型是 E[:] *)
=>
    size_t tmp = (size_t)i;      (* 负数变成很大的无符号数 *)
    if (tmp >= s.len) trap();    (* 运行时陷阱，见 R13 *)
    *(E *)(s.data + tmp * sizeof(E))
```

基类型不是切片时，`x[i]` 保持 ISO C 语义（`*(x + i)`），不带检查。

**R11 — 区间 `s[lo:hi]`**

```
s[lo:hi]                         (* s 是 E[:] 或 E[N] *)
=>
    E[:] out;
    if (lo > hi || hi > len) trap();
    out.data = base + lo * sizeof(E);
    out.len  = hi - lo;
    out
```

`lo` 缺省为 `0`，`hi` 缺省为当前的 `len`（数组取 `N`）。数组基类型的
`base` 是 `&a[0]`；指针不允许出现在 `[lo:hi]` 的左边（C25）。

**R12 — 数组到切片的隐式转换**

```
E[:] s = arr;                    (* arr 的类型是 E[N] *)
=>
    s.data = &arr[0];
    s.len  = N;
```

转换发生在赋值、初始化、函数实参、`return` 与显式强转这些**运行期**
上下文；静态初始化器（文件作用域或 `static` 局部）没有运行时代码，故不支持（C27），
用 `{ arr, N }` 逐字段初始化即可。显式强转 `(E[:])arr` 等价。

**R13 — 越界**

越界（`i >= len`、`lo > hi`、`hi > len`）调用运行时陷阱
`__tcc_slice_*`，它执行一次故意的空指针写。libtcc1 里没有格式化输出
（只用到 `pthread_*` 和最基础的 libc 入口），因此不产生诊断文本，与
`catch` 落空时一致。

**R14 — `foreach |i, v| iterable { body }`**

```
foreach |i, v| a { body }                  (* a 的类型是 E[N] 或 E[:] *)
=>
    pair = { E *data; size_t len };        (* 只求值一次，落在隐藏帧槽 *)
    pair.data = &a[0] / a.data;
    pair.len  = N     / a.len;
    i = 0;                                 (* 两个绑定都是普通局部 *)
    for (; i < pair.len; i++) {
        v = pair.data[i];                  (* 副本；'_' 时连加载都省掉 *)
        body
    }

foreach |i| lo:hi { body }
=>
    T hi_tmp = (T)hi;                      (* T = 两个界的公共整数类型 *)
    for (i = (T)lo; i < hi_tmp; i++) { body }

foreach |v| it { body }                    (* it 的类型提供 has(T *)/next(T *) *)
=>
    /* 三选一：下面三种形态按 it 的类型选一条 */
    T tmp = it;                            (* 值形态：状态复制到隐藏帧槽 *)
    while (has(&tmp)) {
        v = next(&tmp);                    (* 副本；'_' 时丢弃但仍调用 next() *)
        body                               (* continue 回到 has() 判断 *)
    }
    T *tmp = it;                           (* 指针形态：直接操作原对象 *)
    while (has(tmp)) { v = next(tmp); body }
    I tmp = it;                            (* 接口形态：vtable 分派 *)
    while (tmp.vt->has(tmp.self)) {
        v = tmp.vt->next(tmp.self); body
    }
```

生成的代码就是上面那段 `for` / `while`（同一套 `lblock` / `gvtst` /
`gjmp_addr`）。数组/切片/区间没有运行期辅助函数：数组/切片的隐藏槽就是
`c6_slice_spill` 用的 `{data, len}` 对，所以 `foreach |i, v| f()`（`f`
返回切片）与 `foreach |i, v| obj.items` 走同一条路径。迭代器形式只生成
普通的方法调用（具体类型直接调用，接口值经 vtable），没有装箱、没有新的
运行期入口；`_` 绑定时对应的加载/存储整段不生成，但 `next()` 调用仍然
保留（它负责推进状态）。

**R15 — 模块前缀 `module NAME;`**

```
module math;                     (* 文件的第一条声明 *)
int add(int a, int b) { … }      (* => int math__add(int a, int b) *)
struct Point { … };              (* => struct math__Point *)
int area(Point *p) { … }         (* 方法 => int math__Point_area(Point *p) *)

/* 别的文件，import 之后 */
import math;
math.Point p;                    (* 类型 => math__Point *)
```

只有模块文件**文件作用域**的声明加 `NAME__` 前缀；块作用域、`static`、
`test` 块、`__` 开头的名字和 `#include` 进来的普通 C 文件都不加。点号形式
`module geo.circle;` 的前缀是 `geo__circle__`。模块文件不输出方法的
原名弱别名，所以 C 侧看到的只有前缀符号。

类型位置也接受全限定名：`math.Point p;` 解析到 `math__Point` 的 typedef
或 struct/union/enum tag（多级点号 `a.b.Point` 同理）。module 文件里的 tag
带前缀；package 里的 tag 仍是普通 C tag，所以 package 限定类型要写 typedef。
只有当前 module/package 或 `import` 过的名字能这样写；限定名不是类型时（例如
`pkg.func(...)` 这样的表达式语句）解析器会把 token 原样还回去，仍按普通
表达式处理。

`import NAME;` 仍然先登记名字（打开 `NAME.member` 写法）；此外按
「当前文件所在目录 → `-I` 路径 → 当前目录」查找 `NAME.c`（点号形式查找
`a/b.c`），找到就把它的声明读进来：函数只登记原型、跳过函数体，全局变量
只登记 `extern`、跳过初始化器，`static` 对象和 `test` 块跳过。找不到文件
时保持旧行为（只登记名字），所以把 `import` 当命名空间声明的旧代码不受
影响。导入的声明和模块自己的对象文件是两件事：链接时仍要把模块的目标
文件（或包含它的库）放上命令行。

`-emit-header FILE`（模块是 `.c6` 文件时）在编译模块时把导出的结构体、枚举、
typedef、函数和全局变量写成普通 C 头文件，符号名就是带前缀的 C 名。
切片 `T[:]` 和元组 `(T1, T2)` 会生成 `NAME__slice_N` / `NAME__tuple_N`
的 `typedef`；纯 C 侧包含这个头文件即可调用，不需要知道 `module` 的存在。

### 8.1 托管引用的重写

| 源码 | 生成 |
|---|---|
| `@T p = new T;` | `__c6_gc_new(size, 1, &desc, 0)` 的结果经 `__c6_gc_store(&p, v, INIT)` 落进槽（不额外 +1） |
| `p = q;`（`q` 是托管引用） | `__c6_gc_store(&p, q, mode)`：先给 `q` +1，再释放 `p` 的旧值、写槽 |
| `p = NULL;` | 同一次 `__c6_gc_store`：旧值 −1、槽清零（这就是提前放手） |
| `own @T p = new T;` / `= move(q);` | `__c6_gc_own_store` / `__c6_gc_move(&q)`：旧链整条释放，新值不计数 |
| 出作用域 / `return` / `break` / `goto` | 内联 `__c6_gc_release(&p, mode)`（注册成一个清理点，所以每条路径都跑到，见 8.2） |
| 对象计数归零 | 调描述符的 `drop`（若登记过），再按字段表逐个释放字段（数组逐元素、嵌入结构体递归），最后 `free` |
| `go f(p)` / `task t = f(p)` | 实参块 / 任务前缀的那次拷贝 +1，块释放 / `__c6_co_destroy` 时 −1 |
| `thread h = go f(x);` | `h = __tcc_th_spawn(&__c6_sp_f, block)`，作用域登记 `__c6_th_release(h)`（走清理点，每条退出路径都还） |
| `h->join()` / `h->done()` / `h->value()` | `__c6_thread_join(h)` / `__c6_thread_done(h)` / `__c6_thread_value(h)` |
| `t->wait()` / `t->start()` / `t->done()` / `t->kind()` | `__c6_co_wait(t)` / `__c6_co_start(t)` / `__c6_co_state(t) == -1` / `__c6_co_kind(t)`（接收者上转到 `struct __c6_task *`） |

编译器不插入任何运行期调度：没有标记清扫线程，也没有安全点。

### 8.2 `defer` / `errdefer` 的展开

清理点（cleanup site）是**编译期常量**：编号 `id`、所在作用域的深度
`depth`、`kind`（`defer` / `errdefer`）、清理体的标签。运行期状态全部在
函数帧里，没有线程级（TLS / `pthread_key`）状态：

```
(* 函数序言；只有函数里有清理点时才分配 *)
int arm[C6_DEFER_WORDS] = {0};      (* 已注册的清理点：一位一个 site *)
int dtgt, dres;                     (* 正在服务的请求：深度过滤 + 恢复点号 *)
int dtgt_save[8], dres_save[8];     (* 清理体里再发起请求时保存外层 *)

(* 'defer S;' / 'errdefer S;'：注册只置位，然后跳过清理体 *)
arm[id >> 5] |= 1 << (id & 31);
goto after;  S: …;  after:
```

派发（离开作用域、`return`、`err` / `try` 的传播、`break` / `continue`、
`goto`、以及函数末尾的兜底）：

```
    if (narmed == 0) goto rs;               (* 什么都没注册：这个请求没事可做 *)
    dtgt = <filter>;  dres = ++rid;  goto entry[filter];   rs: …   (* 恢复点 *)

entry[key]:   (* 每个 filter 一个入口：第一个可能匹配的 site 的位置 *)
    <该 filter 的请求直接落到这里，不经过下面的开关>

disp:         (* 清理体跑完回到这里 *)
    if (narmed == 0) goto comp;
    switch (dtgt) { case key: goto entry[key]; … }   (* 只有几个比较 *)

    for (site : sites, id 从大到小)          (* 每个 site 一条，共享一条链 *)
        if (arm[site] && dtgt < site.depth && (site.kind == 0 || dtgt == -2)) {
            arm[site] &= ~bit;  narmed--;    (* 无论跑不跑都清掉这一位 *)
            goto site.label;                (* 清理体结尾回到 disp *)
        }

comp:                                       (* 这一轮做完了 *)
    <对 resumes 的 id 二分：找到 dres 那一条，恢复外层 dtgt/dres，跳到它的恢复点>
```

- 深度过滤的编码：mode 0 存 `<depth>`（只跑比它更深的作用域），mode 1 存
  `-1`（整个函数帧），mode 2（错误路径）存 `-2`（整个函数帧，并且放行
  `errdefer`）。负值不可能是合法深度，所以一个 `int` 就够。
- `narmed`（本帧还有几个 site 处于已注册状态）是热路径上的第一道闸：什么都没
  注册的请求（最常见的"块退出但没有 defer 要跑"）一条比较就走人；清理体跑完
  回到 `disp` 时它通常是 0，于是直接进 `comp`，不再扫一遍链。
- 每个 filter 的**入口**由编译期算出：这条链从"该 filter 第一个可能匹配的
  site"开始，所以一个作用域退出只看它正在离开的那几个 site，与函数里总共有
  多少清理点无关（实测：1 个 site 的函数和 41 个 site 的函数，退出一个带
  清理点的块都是 ~10 ns/次）。
- `resumes` 按 id 升序，所以"哪个请求结束了"用二分找（128 个 site 也只有
  7 次比较）。
- `id` 从大到小 = 注册的逆序 = LIFO：同一作用域实例里后注册的先跑，
  跨作用域时深的先跑（它的 `id` 也更大）。
- 位是幂等的：同一个作用域实例里同一个 site 注册两次也只跑一次；离开
  作用域时位被清掉，重新进入就重新注册。
- 全部是本帧的局部变量，所以 ISR、`go` 起来的线程、协程（`async` 的帧就在
  任务里）、递归的每一帧都各跑各的，不需要任何线程级状态。
- 挂起的任务被销毁（`__c6_co_destroy` / 回卷）时发的是 mode 2 的请求，所以
  那次派发会把 `errdefer` 也跑掉：被销毁的任务没有"正常结束"这回事。
- 上限：一个函数最多 128 个清理点（C101，第 129 个报错），清理体最多嵌套
  8 层（C102，第 9 层报错）。

- 帧开销：位图 4 × `int` 加 `narmed` 1 × `int` 在**每个** c6 函数帧里都有并
  在序言清零（单趟编译器在序言处还不知道后面会不会出现清理点）；派发状态
  2 × `int` 和派发器本身只在函数里真有清理点时才有，清理体每嵌套一层再加
  2 × `int`。

## 9. 示例

```c
/* 1. 声明与传播 */
static int!error parse(const char *s)
{
    if (!s)
        err(1, "string is NULL");
    return s[0] - '0';
}

static int!error twice(const char *s)
{
    int v = (try parse(s));      /* 失败时原样传出 */
    return v * 2;
}

/* 2. 就地处理 */
int handle(const char *s)
{
    int v = twice(s) catch e {   /* 拿到 err(id, msg) 的值 */
        printf("caught %d: %s\n", e.id, e.msg);
        return -1;               /* 处理块必须自己离开 */
    };
    return v;
}

/* 3. 不绑定变量 / 带括号 / goto 离开 */
int alt(const char *s)
{
    twice(s) catch {
        goto failed;
    };
    twice(s) catch (e) {
        goto failed;
    };
    return 0;
failed:
    return -1;
}

/* 4. 无返回值的 T!error */
static void!error check(int n) { if (n < 0) err(2, "negative"); }

int use(int n)
{
    try check(n);                /* 语句形式：返回值被丢弃 */
    check(n) catch e { return -1; };
    return 0;
}
```

`catch` 同时清空错误槽，所以连续处理两次不会串扰：

```c
    f() catch e { return 1; };   /* flag 已清零 */
    g() catch e { return 2; };   /* 不会误判为 g 失败 */
```

包、方法与接口一起用（多级包 + 自动 vtable）：

```c
interface Shape { int area(); }

struct Circle { int r; };
struct Rect { int w, h; };
typedef struct Circle Circle;
typedef struct Rect Rect;

/* 方法符号按接收者取名（Circle_area / Rect_area），同名方法不再需要包；
   这里演示包、方法与接口一起用 */
package geo.circle {
    int area(Circle *self) { return 3 * self->r * self->r; }
}

package geo.rect {
    int area(Rect *self) { return self->w * self->h; }
    int twice(Rect *self) { return self->area() * 2; }   /* 方法可调方法 */
}

import geo.circle;
import geo.rect;

int describe(Shape s) { return s.area(); }   /* 结构体值 -> 接口值 */

int main(void)
{
    Circle c = {2};
    Rect r = {2, 3};
    return describe(c) == 12 && describe(r) == 6 &&
           (&r)->twice() == 12 && geo.rect.area(&r) == 6 ? 0 : 1;
}
```

**R15 — `async T f(P…) { body }`**

```
int __c6_body_f(void *__c6_arg, void *__c6_frm);
const struct __c6_desc __c6_desc_f =
    { sizeof(struct f_task), alignof, __c6_body_f, __c6_rel_f, frame_size };
```

函数体按 `int __c6_body_f(void *task, void *frame)` 编译。`frame` 是运行期
给出的帧顶：任务块一次分配出 `struct f_task` + 帧（帧里带着自己的清理状态），后端
把帧寄存器（x86 `rbp`、arm64 `x29`、riscv64 `s0`、arm `fp`、C99 后端的
`__c99_frmp`）直接指向那块内存，`task` 存在帧的第一个槽里。序言再看
`__c6_co_state(task)`：非 0 就调用 `__c6_co_restore(task, base)`（帧已住在
任务里时它只把错误槽放回去）并跳到恢复分发器，为 0 就正常往下执行。
声明的形参在此时被声明成普通局部变量，并从 `task->pN` 拷进来（只有首次进入
会走这条路径）。描述符第 3 个成员是 body，第 4 个是 `-c9x` 出来的释放函数，
第 5 个是后端回填的帧大小：0 表示"帧不能住在任务里"（c67），退回拷贝模型。

`struct f_task` 由 `c6_async_declare()` 用合成 token 在文件作用域声明，
所以同一个 `async` 声明在定义方和调用方得到同一个布局。

**R16 — `await`**

```
    x = await f(a)          ⇒  task = __c6_co_new(&__c6_desc_f);
                               task->p0 = a;
                               if (__c6_co_await(task, self))
                                   __c6_co_suspend(self, KIND_CHILD, N, base, size);
                                   return KIND_CHILD;     (* 回到驱动方 *)
                               N:  x = task->val;          (* 恢复后从这里继续 *)

    await c6_io(fd, ev)     ⇒  __c6_co_set_io(self, fd, ev);
                               __c6_co_suspend(self, KIND_IO, N, base, size);
                               return KIND_IO;
                               N:  __c6_co_rev(self);      (* 就绪掩码 *)

    return e;               ⇒  self->val = e;
                               <派发本帧的清理点，见 8.2>
                               __c6_co_finish(self, KIND_DONE);
                               return KIND_DONE;
```

挂起时只把错误槽存进任务，然后 body 直接 `return` 回到驱动方；恢复时把它
取回来，再跳到该号恢复点。帧本身不用动（它就在任务的那块内存里，地址在
任务的一生里不变），所以**局部变量、清理点的注册状态、错误状态在挂起前后
完全不变**，`&local` 跨过 `await` 也仍然有效，帧没有 4 KiB 上限。

后端若回答"帧不能住在任务里"（目前只有 c67），则退回拷贝模型：每次挂起把
`[base - size, base)` 的原生帧拷进任务（上限 4 KiB），恢复时拷回。那里帧内
的绝对地址不可跨 `await` 使用，`weak` 局部也被禁止（约束 C67）。

驱动方 `__c6_co_wait(task)` 反复进入 `__c6_body_f`：每次拿到的 `kind` 是
`DONE` / `IO` / `CHILD`，为 `CHILD` 时先往子任务里钻，为 `IO` 时用
`select()` 等 fd 就绪并把掩码放进 `wait_rev`。

**R17 — `co f(a);` / `task t = f(a);`**

```
    co f(a)                 ⇒  h = __c6_co_new(&__c6_desc_f);   (* 同 R15 的调用 *)
                               h->p0 = a;
                               __tcc_co_spawn(h);
                               (* 返回时 h 可能已经跑完并被回收 *)

    task t = f(a)           ⇒  h = __c6_co_new(&__c6_desc_f);
                               h->p0 = a;
                               t = h;                        (* 普通局部，类型 = struct f_task * *)
                               (* 作用域退出时：__c6_co_destroy(t) *)
```

`__tcc_co_spawn()` 置 `parent = 0`、把任务挂到顶层就绪链表，然后反复步进
直到它在 fd 上 park 或结束；结束的任务当场 `__c6_co_destroy`，以 `err`
结束的只把计数留给下一次 `__c6_co_run()`。`task` 的清理
登记就是一个编译器合成的清理点（同 8.2），因此和手写
`defer { __c6_co_destroy(t); }` 等价。

**R18 — 元组类型与元素访问**

```
    (T1, T2) t;                 ⇒  struct __c6_tuple_N t;   (* 自然对齐布局 *)
    t[k]                        ⇒  (*((T_{k+1} *)((char *)&t + off_k)))
    t[k] = v                    ⇒  (*((T_{k+1} *)((char *)&t + off_k))) = v
```

`off_k` 与 `__c6_tuple_N` 的总大小在**构造类型时**一次算完并缓存，因此
`t[k]` 不生成任何函数调用，也不依赖 `#pragma pack`。同一个元素类型序列
只规范化出一个内部 tag，所以 `(int, int)` 在任何位置、任何 TU、任何
`#pragma pack` 状态下都是同一个 `CType`。

**R19 — 解构声明**

```
    (int q, int r) = expr;
=>
    (int, int) __c6_tuple_tmpK = expr;      (* 隐藏局部，左值 *)
    int q = __c6_tuple_tmpK.__c6_t0;
    int r = __c6_tuple_tmpK.__c6_t1;
```

`_` 位置的语句不生成（不建符号、不生成加载）。`expr` 只求值一次。

**R20 — 解构赋值**

```
    (a, b) = expr;                          (* a、b 是左值 *)
=>
    (T1, T2) __c6_tuple_tmpK = expr;
    a = __c6_tuple_tmpK.__c6_t0;
    b = __c6_tuple_tmpK.__c6_t1;
```

逐元素走普通赋值（含通常的赋值类型检查），因此目标可以是变量、数组元素、
`->` / `.` 成员、`*p` 等任何左值；元素个数不符是 C55。

**R21 — `return { … };`**

```
    (T1, T2) f(void) { return {a, b}; }
=>
    (T1, T2) __c6_ret;                      (* 落在返回槽里 *)
    __c6_ret.__c6_t0 = a;
    __c6_ret.__c6_t1 = b;
    return __c6_ret;
```

同样适用于 struct / union 返回值（C59 只要求返回类型是这三种之一）。

**R22 — `!error` 的聚合返回值**

```
    return $S(T);                           (* 失败路径：$S(T) 是聚合类型 *)
=>
    memset(&__c6_ret, 0, sizeof(T));        (* 全零，而不是 -1 *)
    return __c6_ret;
```

失败与否只由错误槽（`__tcc_err_flag_peek`）判定，返回值只承载成功路径的值，
而 C15 又强制 `T!error` 的返回值必须被 `try` / `catch` 消费，因此失败路径上
那个全零的值永远不可观测。标量返回类型仍用哨兵 `-1`（C9）。

**R23 — 函数字面量**

```
    int(int x) { return x + 1; }                       (* 写在表达式位置 *)
=>
    static int __c6_fn_1(int x);                       (* 翻译单元末尾发射 *)
    …                                                  (* 表达式本身：__c6_fn_1 *)
```

字面量在**原地**只留下函数指示符（和写 `f` 一样退化成 `int (*)(int)`），
体被存起来，在翻译单元末尾用 `gen_function()` 单独编译——与 `static inline`
函数走同一条路。所以：

* 字面量不产生任何运行期数据（`sizeof` 一个函数指针仍是 8），也不参与
  引用计数：域里存的是裸函数地址；
* 体里出现的外层块作用域名字无法携带，编译期就报 C80；
* `static` 让它在 `-emit-header` 与 `module` 前缀改写之外，名字里的序号
  只有调试信息看得到。

V2 不改这条重写规则的无捕获部分：捕获集合为空时，表达式仍然只留下函数
指示符。捕获集合非空时，原地多发射一个 env 和一个隐藏的 `{code, env}` 闭包
对象，表达式的值是这个对象的一个 `@` 引用；函数符号本身多一个隐藏的
`void *__c6_env` 首参。详见 [closure-design.md](closure-design.md) §6。

**R24 — `f".."` / `@f".."`**

```
    f"a{n}b"                                     (* const char * *)
=>
    __c6_fmt_beg();                              (* 压一个构造器到本线程的栈上 *)
    __c6_fmt_l("a", 1);                          (* 文本片段：立即拷进缓冲 *)
    __c6_fmt_i((long long)(n), 0);               (* 插值：按类型选一个 __c6_fmt_* *)
                                                 (* 第二个实参是格式说明的位编码，
                                                    没有说明时是 0 *)
    __c6_fmt_l("b", 1);
    __c6_fmt_end(0)                              (* 借用：文本在本深度的缓冲里 *)

    @f"a{n}b"                                    (* @char *)
=>
    … 同上 …
    __c6_fmt_end(1)                              (* 拷成托管对象，计数 1 *)
```

每一段按**静态类型**选调用：

| 插值的类型 | 生成的调用 |
|---|---|
| `char` / `signed char` / `unsigned char` | `__c6_fmt_ch((int)x, spec)` |
| `short` / `int` / `long long` / `enum`（有符号） | `__c6_fmt_i((long long)x, spec)` |
| 同上但无符号 | `__c6_fmt_u((unsigned long long)x, spec)` |
| `_Bool` | `__c6_fmt_b((int)x, spec)` |
| `const char *`（含 `char[N]`） | `__c6_fmt_c(p, spec)` |
| 托管 `@char` | `__c6_fmt_s(p, __c6_str_len(p), spec)` |
| 其它指针 | `__c6_fmt_p((const void *)x, spec)` |
| 字符切片 | `__c6_fmt_sl(&x, spec)` |
| `float` / `double` / `long double` | `__c6_fmt_f((double)x, spec)` |

`spec` 是格式说明的位编码（宽度 10 位、精度 10 位、conv 4 位、flags 5 位），
由编译器在编译期解析好；没有写说明时是 0，运行时的默认行为就是上一节
「按静态类型输出」那一套。

构造器**不带指针参数**：`__c6_fmt_beg()` 从每线程的槽数组里按嵌套深度
取一个（深度在编译期就是已知的调用配对），所以嵌套、递归、循环互不干扰。
`__c6_fmt_end(0)` 返回该槽的缓冲（下一次同深度的 `f".."` 会覆盖它，见
C84 的借用说明）；`__c6_fmt_end(1)` 把文本拷成一个 `@char` 对象并返回它。

---

切片配合数组、函数与结构体：

```c
typedef int Row[3];
struct Pair { int a, b; };          /* 切片元素的结构体要在文件作用域 */

static int sum(int[:] s)
{
    int i = 0, total = 0;
    for (i = 0; i < (int)s.len; i++)
        total += s[i];
    return total;
}

int main(void)
{
    int a[5] = {1, 2, 3, 4, 5};
    int[:] s = a;                   /* 数组 -> 切片 */
    int[:] mid = s[1:4];            /* 区间，mid.len == 3 */
    int[:] rest = mid[1:];          /* 对切片再切片 */
    char[:] text = "hi";            /* 字符串字面量：len 含结尾 NUL */

    int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
    Row[:] rows = grid;             /* 元素是数组：先 typedef */

    struct Pair pairs[2] = {{1, 2}, {3, 4}};
    struct Pair[:] ps = pairs;      /* 元素可以是结构体 */
    ps[1].a = 30;

    int[:] view = (int[:]){ a + 2, 2 };   /* 指针 + 长度 */

    return sum(s) == 15 && mid[0] == 2 && rest.len == 2 &&
           text.len == 3 && rows[1][2] == 6 && ps[1].a == 30 &&
           view[0] == 3 ? 0 : 1;
}
```

跨翻译单元：两边各自写 `int[:]`，布局固定，直接按值传递/返回即可。

`foreach` 是上面这些遍历写法的语句形式：

```c
struct Pair { int a, b; };
struct Countdown { int n; };
int has(struct Countdown *s) { return s->n > 0; }
int next(struct Countdown *s) { return s->n--; }

int main(void)
{
    int a[5] = {1, 2, 3, 4, 5};
    int[:] s = a[1:4];                      /* 2 3 4 */
    struct Pair pairs[2] = {{1, 2}, {3, 4}};
    struct Countdown countdown = {3};
    int total = 0;

    foreach |i, v| a {                      /* 数组：下标 + 元素副本 */
        total += v;                         /* i 的类型是 size_t */
    }
    foreach |i, v| s {                      /* 切片：同样两个绑定 */
        total += v;
    }
    foreach |i| 0:3 {                       /* 区间 [0, 3)：只要一个绑定 */
        total += i;
    }
    foreach |_, v| a {                      /* '_' 丢弃下标 */
        total += v;
    }
    foreach |i, p| pairs {                  /* 元素是结构体时也是副本 */
        p.b = 0;                            /* 不改 pairs */
    }
    foreach |v| countdown {                 /* 迭代器：int has(T *); E next(T *) */
        total += v;                         /* v 是 next() 返回值的副本 */
    }
    return total == 48 ? 0 : 1;             /* 15 + 9 + 3 + 15 + 6 */
}
```

### 9.1 async / await：一次 HTTP 请求

```c
/* 传输层：每个原语自己决定"等什么 fd" */
async int connect_to(int wfd)
{
    int rev = 0;
    rev = await c6_io(wfd, C6_CO_EV_WRITE);
    if (rev <= 0)
        return -1;
    return wfd;
}

async int send_all(int wfd, const char *data, int len)
{
    int off = 0;
    int n = 0;
    while (off < len) {
        await c6_io(wfd, C6_CO_EV_WRITE);
        n = write(wfd, data + off, len - off);
        if (n <= 0)
            return -1;
        off += n;
    }
    return off;
}

/* 业务层：三个 await 顺着写，读起来和同步代码一样 */
async int http_get(int wfd, int rfd, const char *req, char *buf, int cap)
{
    int fd = 0;
    int sent = 0;
    int status = 0;

    fd = await connect_to(wfd);
    if (fd < 0)
        return -1;
    sent = await send_all(fd, req, (int)strlen(req));
    if (sent < 0)
        return -2;
    status = await recv_status(rfd, buf, cap);
    return status;
}

int main(void)
{
    struct http_get_task *t = http_get(up_w, down_r, "GET / HTTP/1.1\r\n\r\n",
                                       buf, sizeof buf);
    int rc = __c6_co_wait(t);       /* 驱动到完成：0 = 正常结束 */
    printf("status=%d\n", t->val);
    __c6_co_destroy(t);
    return rc;
}
```

完整可运行的版本见 `examples/async_demo.c6`（用 `pipe()` 代替 socket，
离线就能跑）；自测见 `tests/async.c6`。

## 10. 未支持的写法

以下写法**不是**本规范的一部分，编译器会报错：

| `p->drop()` / `drop(p)` 手动调析构 | 约束 C76（对象只由引用决定生死） |
| `reset()` 成员 | 不提供：提前放手就是 `p = NULL` |
| `@T`/`own @T` 进 `go`/`async` | 约束 C73 |
| `weak @T` 在 `async` 函数里 | 允许（帧住在任务里）；只有 c67 这类帧拷贝后端报约束 C67 |
| 通用图（`struct Node { @Node edges[4]; }`） | 约束 C66：环一律报错，只能写成 `own` 树或 `weak` 边 |
| 运行期环回收（cycle collector） | 不做：环在编译期就挡住了 |
| 自定义删除器 | V1 不做（析构只有 `drop` 方法一种） |
| 写法 | 结果 |
|---|---|
| 捕获式闭包：`closure int() f = int() { int x = 1; return x; };` | 按值捕获进隐藏 env（V2 已落地），类型写成 `closure int()`；无捕获的字面量仍是裸函数指针，捕获了值却塞进普通函数指针报 C93（见 [closure-design.md](closure-design.md)） |
| `task t = void() { … };` 之类包装 | 不需要：字面量本身就是函数指针，直接 `go f(x)` / 直接传 |
| `try { … } catch { … }` 块形式 | 语法错误（`try` 只接一次调用） |
| `try a = f();` | 语法错误（`try` 只接调用表达式） |
| `catch` 处理块自然落到末尾 | 约束 C6 |
| `int v = 0; try v;` | 约束 C1/C2 |
| `double!error f(void)` | 约束 C9：`float`/`double` 既没有哨兵也不是聚合类型；struct / union / 元组可以 |
| `int!error x;` | 约束 C8 |
| `try` / `catch` / `err` 写在 `defer` 体内 | 约束 C11 |
| `err();`（无参数重抛） | 约束 C16，请改用 `try` |
| `void f(error e);` / `void f(error *e);` | 约束 C18：错误只从返回值出来 |
| `f() catch e;`（无处理块） | 语法错误 |
| `catch e { … }` 单独出现 | 约束 C7 |
| `int x;` / `int a[3];`（不带初始化器的定义） | 约束 C19，`static` 变量与 VLA 除外 |
| `Vec { int x, y; };`（定义处省略 `struct`） | 仍要写 `struct`：裸 `TAG { … }` 分不清 struct/union，也容易和初始化器混淆 |
| `stat st;`（tag 名被函数/变量遮蔽） | 约束 C41：按普通 C 解析，需要类型时写 `struct stat` |
| `S *p;` 而 `S` 未声明 | 约束 C41：不会隐式前置声明 tag，报 `'S' undeclared` |
| `int x = Vec;`（tag 当值用） | tag 只在类型位置生效：`'Vec' undeclared` |
| `math.add(1, 2)` 而此前没有 `import math;` | 约束 C14 |
| `impl Shape for Circle { … }` | `impl` 不再是关键字，按普通声明解析而报错；改用「结构体拥有方法」实现接口 |
| `v.f()`（值是结构体而不是指针） | 按普通成员访问报 `field not found: f`；方法调用只支持 `->` |
| `p->f`（方法不带调用） | 约束 C20：`method 'f' must be called` |
| 方法在使用点之后才声明 | 约束 C20/C21（TCC 单趟编译） |
| `area(p)` 而 `area` 服务两个结构体 | 普通调用名是弱别名，只指向本 TU 里第一个取到该名字的方法；要明确调用写 `p->area()` |
| 用纯 C 定义的方法被 `p->f()` 调用 | 链接错误 `unresolved reference to 'T_f'`：`->` 只认 c6 编译出的方法符号，改用 `f(p)` |
| 文件作用域 `Shape s = circle;` | 结构体到接口的转换没有运行时代码可生成 |
| `Shape s = circle;` 而 `circle` 的方法声明在转换点之后 | 约束 C21 |
| `int[]` 当切片用 | 不是切片语法，仍是 C 的数组声明符（`int a[] = {…}`、`void f(int[])`） |
| `p[1:2]`（指针上的区间） | 约束 C25，请写 `(T[:]){p, n}` 再取区间 |
| `s[0:2:4]`（容量形式） | 约束 C26：切片没有 cap |
| `int[3] a;` | 数组声明符仍然只能写在名字上（C 本身如此），元素类型先用 `typedef` |
| 函数内局部 `struct` 作为切片元素 | 约束 C23 |
| `int[:] g = arr;` / `static int[:] g = arr;` | 约束 C27，写 `{ arr, N }` |
| VLA / 不完整数组转切片 | 约束 C24 |
| `append` / `cap` / 动态扩容 / 深拷贝 | 不属于切片语义（视图，不拥有元素） |
| `int x; x += await f();` 这类把中间值留在寄存器里的写法 | 一般可用（调用点会先把活寄存器落到帧槽），静态判断不出来时报约束 C33 |
| `await` 写在 `defer` 体 / `sizeof` 里 | 约束 C31 |
| `c6_io(fd, ev)` 不加 `await` | 约束 C32 |
| 函数指针 / 数组 / VLA 作为 `async` 函数的形参或返回值 | 约束 C29：任务结构要能从声明写回，这几类写不回去 |
| `async` 体里的 VLA 局部量 / `alloca` | 约束 C87：栈上分配的空间在挂起之后不归这个 body（恢复时栈指针回到序言的位置） |
| `async` 嵌套定义、`async inline`、`async int main` | 约束 C29 |
| 用 `setjmp`/`ucontext`/独立栈实现的有栈协程 | 不在本扩展范围：`async` 体始终跑在调用方的栈上 |
| `co f();`（`f` 是普通函数） | 约束 C35：普通函数用 `go` |
| `co` / `go` / `task` 写在函数外 | 约束 C34 |
| `go f();`（`f` 是 `async`） | 约束 C38：协程用 `co` |
| `go p();`（`p` 是函数指针） | 约束 C38：跳板要按被调函数生成 |
| `go f();` 而 `f` 的形参是数组 / VLA / 函数指针 | 约束 C38：参数块写不回去 |
| `go vf(1);`（`vf` 是变参函数） | 约束 C39 |
| `go ff();`（`ff` 带 `!error`） | 约束 C40 |
| `go f();` 而 `f` 只有块作用域声明 | 约束 C38：需要文件作用域声明才能生成跳板 |
| `task t;`（不带初始化器） | 约束 C36：句柄类型从初始化器推出 |
| `async` 函数当方法用（`p->f()`） | 约束 C37 |
| `await c6_io(fd, ev)` 的 `fd` 不在 `[0, 1024)` | 任务按失败回收：`__c6_co_wait` 返回 `-1`，`__c6_co_run` 计一次失败（不会永远 park） |
| 抢占式调度、把阻塞的 libc 调用（`read`/`printf`/…）自动变成让出点 | 不在本扩展范围：`co`/`__c6_co_run` 只推进协程，不 hook libc |
| PE 目标上的 `go` | 还没有 PE 线程支持，运行时 trap |
| 取消 / 强杀一个 `go` 出去的线程 | 不提供：线程总是被启动它的线程 join |
| `co` 的句柄 | 不提供：`co` 把任务交给顶层就绪链表，任务一结束就被销毁，句柄会悬空。要句柄用 `task t = f(a); t->wait();` |
| `thread h = f(a);`（不是 `go`） | 约束 C78：句柄只能来自 `go` |
| 等待线程时取非整数/指针的返回值 | `h->value()` 只有整数和指针能往返；结构体、浮点用 `shared @T` 或参数块带回 |
| `for a \|i, v\| { }` / `for 0..n` | `for` 保持纯 C；迭代请用 `foreach` |
| `foreach a { }`（不写绑定表） | 语法错误：绑定表必须紧跟 `foreach` |
| `foreach (a) \|i, v\| { }`（括号形式） | 不支持：`foreach` 后面直接跟绑定表 |
| `foreach \|i, v\| a v = 1;`（没有花括号） | 约束 C50（三种形态都一样：数组/切片、区间 `\|i\| lo:hi`、迭代器 `\|v\| it`） |
| 文件作用域的 `foreach \|i, v\| a { }` | 约束 C42 |
| `foreach \|i, v\| p { }`（`p` 是指针） | 约束 C47：先写 `(T[:]){p, n}` |
| `foreach \|i, v\| 5 { }`（整数） | 约束 C47：整数要先写成区间 `0:5` |
| `foreach \|i, v\| s { }`（`s` 是没暴露切片的 `struct`） | 约束 C47：用类型暴露的切片成员，例如 `foreach \|i, v\| s.items` |
| `foreach \|i, v\| vla { }` | 约束 C47：VLA 的长度运行期才知道 |
| `foreach \|i, v\| a { }` 而 `a` 的元素是数组（`int a[2][3]`、`Row[:]`） | 约束 C49：元素是数组时不能按值复制，改用下标区间 `foreach \|i\| 0:(int)rows.len { … rows[i] … }` |
| `foreach \|_, _\| a { }` | 约束 C46：两个绑定都丢弃没有意义 |
| `foreach \|i\| a { }` / `foreach \|i, v\| 0:n { }` | 约束 C44/C45：绑定数量由可迭代对象决定 |
| `foreach \|i, v\| it { }`（迭代器） | 约束 C100：迭代器只有一个值绑定，要下标自己维护计数器 |
| `foreach \|v\| it { }` 而 `it` 的类型只有 `has()` 没有 `next()`（或有同名字段遮蔽） | 约束 C47：按“没有迭代器协议”处理；补上 `E next(T *self)` |
| `void *next(T *self)` | 约束 C99：元素类型会被擦除；改成具体的 `E` 或 `E *` |
| `has()` 返回非整数 / `next()` 返回数组、`void`、不完整类型 | 约束 C99/C49 |
| `has()`/`next()` 带多余形参 | 约束 C99：两个方法只接收 receiver |
| `Vect<int>` / `Iterator<T>`（尖括号） | 不支持：尖括号永远不是泛型写法，类型应用只有 `@` 一种，见 [generics-design.md](generics-design.md) |
| 泛型 `Vect@int` / `struct Vect@T` | 已落地：见 §3.11 与 [generics-design.md](generics-design.md)；实例名带模块前缀、`-emit-header` 可导出；容器迭代仍建议 `foreach` |
| 体内成员函数 `struct Vect@T { T at(int i) { … } }` | 已落地：隐式 `self`、方法可先用后定义、符号仍是 `Vect_int_at`，见 §3.11 |
| 泛型 `\|T\| T vat(Vect@T *)`、`\|T\| typedef Vect@T MyVec` | 已落地：一份模板覆盖所有实例，`tests/generic_fn.c6` |
| 泛型值形参 `Buf@(T, 4)` / `struct Buf@(T, int N)` | 已落地：实参是常量表达式，体内当常量用（可作数组长度），见 §3.11 |
| `(int) t[0]` 里的 `(int)` 当一元组 | 约束 C54：一元括号仍是强转，元组至少两个元素 |
| `t[i]`（运行期下标）/ `t[-1]` | 约束 C58：下标必须是整数常量且在范围内 |
| `t = {1, 2}` / `f({1, 2})`（元组字面量糖） | 不支持：显式写复合字面量 `(int, int){1, 2}` |
| `(int q, int r) = 5;` | 约束 C57：右边必须是元组 |
| `(int a, int) = f();` / `(a, int b) = f();` | 约束 C56：模式不能混写，退回普通表达式解析而报错 |
| `(_, _) = f();` | 约束 C60：全丢弃应该直接写 `f();` |
| `((int a, int b), int c) = f();`（嵌套解构） | 不支持：先解构外层，再解构内层 |
| `(q, r) := f();`（`:=` 类型推断） | 不支持：解构声明要把元素类型写全 |
| 函数里定义的 struct 当元组元素 | 约束 C52：元素类型必须在文件作用域可见 |
| `(void, int)` / `(int[3], int)` / `(int f(), int)` / `(error, int)` | 约束 C51 / C53 |
| 用 `#pragma pack` 改变元组布局 | 元组固定按自然对齐：跨 TU 的 ABI 不能随 pragma 状态变化 |
| `(int, int) t; &t[0]` / 把元组直接当 `T[:]` 的底层数组 | 不支持：元素之间可能有 padding，不保证连续 |
| `(int[2], int)`（数组作元素） | 约束 C51：数组不可赋值，请用指针、切片 `T[:]` 或包一层 struct |
| `((int,int){1,2}[0])`（复合字面量外面再套一层括号、下标写在括号里） | 报 `')' expected (got '[')`：写成 `(int,int){1,2}[0]` 或 `((int,int){1,2})[0]` |
| 用元组（或 struct）作 `interface` 方法签名里的参数/返回类型 | **支持**（`tests/iface_agg.c6`）；唯一的限制是类型要在 interface 声明之前可见 |
| 在 `interface` 签名里第一次提到 `struct P`、之后才定义它 | 接口的方法列表按函数原型重放，那里的 tag 属于原型作用域（C11 6.2.1p7），和文件作用域的 `struct P` 不是同一个类型：把 `struct P;` 写在 `interface` 之前（诊断会提示 prototype-scope tag） |
| `return {a, b};` 而返回类型是标量或 `void` | 约束 C59 |
| `0..count`（点号区间） | 词法上 `0..3` 是 `0.` 与 `.3` 两个浮点常量，改词法会波及现有代码；统一写 `0:count` |
| `f"{x}"` 而 `x` 是结构体、函数本身（`f"{main}"`）、非字符切片 | 约束 C84：结构体和函数不能插值；函数**指针**变量按普通指针输出地址 |
| `f"{x;x}"` 而 `x` 是浮点 / `f"{n;f}"` 而 `n` 是整数 | 约束 C89：浮点只收 `f F e E g G`，整数没有 `f` |
| 希望浮点输出与 `printf` 逐位一致 | 运行时自己算数字：常见范围精确，17 位有效数字的最后一位、以及「五成双」的舍入可能与 libc 不同；`\|v\| >= 1e19` 用科学计数法 |
| `f"{(try g())}"` / `f"{(a;b)}"` | 约束 C88：插值里不能传播错误，`;` 只能用来引入格式说明 |
| `f"{n;z}"` / `f"{s;x}"` / `f"{n;2000d}"` / `f"{n;}"` | 约束 C89：格式说明要合法且与类型相容 |
| `#define M(n) f"x{n}"` 里的 `{n}` | 宏形参在插值里不展开：先落成变量，或不用宏 |
| 同一表达式里两个 `f".."` 同时使用 | 借用缓冲按嵌套深度只有一块，后一个覆盖前一个；要同时活着写 `@f".."` |
| `f"{c}"` 想得到字符 `c` 的数值 | 插 `char` 输出的是字符本身，数值写 `{(int)c}` |
| `:` 形式的格式说明（`{x:>8}`、`{x:.2f}`、`{x=}`） | 不提供：`:` 在 C 里是三元运算符的一部分，格式说明用 `;` 引入（见 5.1） |
| 文件作用域或 `static` 局部初始化式里的 `f".."` / `@f".."` | 约束 C85：不是常量表达式 |
| `f "{x}"`（`f` 与引号之间有空白） | 词法上不是 f-string：那是标识符 `f` 后面跟一个字符串字面量 |
| 把 `f".."` 的结果存下来长期使用（不带 `@`） | 借用：下一次同深度的 `f".."` 会覆盖它；要留住写 `@f".."` |

---

## 11. 实现位置

| 语法 | 实现 | 位置 |
|---|---|---|
| `package` / `import` | `c6_package_parse` / `c6_import_parse` / `c6_pkg_name` / `c6_pkg_compose` / `c6_pkg_imported`，`unary()` 里的点号解析 | `tcc/tccgen.c` |
| `pkg.member` | `unary()` 的 `tok_identifier` 分支（逐段增长前缀 + `sym_find_raw`） | `tcc/tccgen.c` |
| 结构体方法 | `c6_method_register`（在 `decl()` 里注册）/ `c6_method_symbol`（`<Tag>_<name>` 符号名）/ `c6_method_plain_decl` + `c6_method_alias`（源码名弱别名）/ `c6_method_lookup` / `c6_has_field` / `c6_method_call_prep`，`unary()` 的 `->` 分支 | `tcc/tccgen.c` |
| `interface` | `c6_interface_parse`、`c6_auto_impl_ensure` / `c6_check_method` / `c6_emit_auto_impls` / `c6_iface_convert` | `tcc/tccgen.c` |
| 每 TU 状态复位 | `c6_tu_reset`（在 `tccgen_compile` 开头调用） | `tcc/tccgen.c` |
| `test` | `c6_test_parse`、`tcc_run_tests` | `tcc/tccgen.c`、`tcc/tccrun.c` |
| `defer` / `errdefer` | `c6_defer_parse`、`c6_defer_begin`、`c6_defer_func_end`（运行期状态在函数帧里，见 8.2；`libtcc1` 里没有 defer 运行时） | `tcc/tccgen.c` |
| `err` | `c6_err_parse` / `c6_emit_propagate` | `tcc/tccgen.c` |
| `co` | `c6_co_parse`、`__tcc_co_spawn` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| `thread` | `c6_thread_decl`、`__tcc_th_spawn` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| `go` | `c6_go_parse` / `c6_sp_parse` / `c6_sp_emit` / `c6_sp_flush`、`__tcc_sp_args` / `__tcc_th_start` / `__c6_th_join_all` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| `thread` 句柄 | `c6_thread_decl` / `c6_thread_defer_release`、`__tcc_th_spawn` / `__c6_th_release` / `__c6_thread_*` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c`、`tcc/c99-rt.inc` |
| 句柄方法 | `c6_method_lookup` + `c6_task_head_ptr`（`t->wait()`）、预置方法声明（`h->join()`） | `tcc/tccgen.c`、`tcc/tccpp.c` |
| 软关键字 | `c6_soft_kw` / `c6_soft_kw_shadowed`（`block()` 与 `decl()` 两个入口） | `tcc/tccgen.c` |
| 每线程状态 | `tcc_th()` / `tcc_th_join_children`（`pthread_key`） | `tcc/lib/libtcc1.c` |
| `task` | `c6_task_decl` / `c6_task_check_handle` / `c6_task_defer_destroy` | `tcc/tccgen.c` |
| 顶层调度（64 槽就绪链表 + 一次 `select()`） | `tcc_co_pump` / `tcc_co_step_once` / `tcc_co_link` / `tcc_co_unlink` / `__c6_co_run` | `tcc/lib/libtcc1.c` |
| `!error` | `c6_parse_fallible_suffix` / `c6_apply_fallible`，`FuncAttr.func_fallible` | `tcc/tccgen.c`、`tcc/tcc.h` |
| `f".."` 切分 | `parse_fstr_string` / `fstr_lex` / `fstr_add_lit` / `fstr_unquote`，`TOK_C6_FSTR` → `TOK_C6_FSTR_BEG` … `TOK_C6_FSTR_END` | `tcc/tccpp.c`、`tcc/tcctok.h` |
| `f".."` 下降 | `c6_fstr_expr` / `c6_fstr_lit` / `c6_fstr_piece`（`unary()` 里的 `TOK_C6_FSTR_BEG` 与 `'@'`） | `tcc/tccgen.c` |
| `@f".."` 的托管结果 | `C6_GC_FRESH` + `C6_GC_ATOMIC` 的 `__c6_fmt_end` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| f-string 运行时 | `__c6_fmt_beg` / `__c6_fmt_l` / `__c6_fmt_s` / `__c6_fmt_c` / `__c6_fmt_ch` / `__c6_fmt_sl` / `__c6_fmt_i` / `__c6_fmt_u` / `__c6_fmt_p` / `__c6_fmt_b` / `__c6_fmt_end` / `__c6_str_len` | `tcc/lib/libtcc1.c` |
| f-string 的 C 后端 | 同一组 `__c6_fmt_*` 的 C 实现、`c99_runtime_names[]` 里的声明 | `tcc/c99-rt.inc`、`tcc/c99-gen.c`、`tcc/tccpp.c` |
| `try` | `c6_try_parse`（`unary()` 的前缀分支） | `tcc/tccgen.c` |
| `catch` | `c6_catch_parse`（`expr_catch()` 层，位于 `expr_cond()` 之下） | `tcc/tccgen.c` |
| `#macro` / `__local` | `parse_c6_macro` / `c6_hygiene_body` | `tcc/tccpp.c` |
| 变量初始化 | `c6_check_var_initialized`（在 `decl()` 的对象定义分支调用） | `tcc/tccgen.c` |
| 裸 tag 类型名 | `parse_btype()` 的 `default` 分支：`sym_find()` 未命中时试 `struct_find()`，命中则走 `basic_type2`（与 `struct TAG` 同一条路径）；`ignore_label` 时 `TAG:` 退回标签 | `tcc/tccgen.c` |
| 枚举成员 `Color.RED` | `struct_decl()` 的枚举循环（`<tag>.<成员>` 额外注册一个带点的常量名；裸名被别的枚举占用时记进 `c6_enum_poison`，裸用处报 C119）、`unary()` 的 `tok_identifier` 点号分支（先按带点名字查枚举常量，再按包名 mangle）、`decl()` 在条件位置的向前窥探（`Tag.` 一定不是声明，交回 `gexpr()`） | `tcc/tccgen.c` |
| `T[:]` 类型 | `parse_btype()` 的 `case '['`、`c6_slice_get`（按元素类型规范化生成内部 struct） | `tcc/tccgen.c` |
| `(T1, T2)` 元组类型 | `parse_btype()` 的 `case '('`（`c6_ext` 且还没有 `typespec_found` 时）：`c6_tuple_btype` 扫平衡括号、`c6_tuple_speculate` 重放 token 试一遍 `type_decl(TYPE_ABSTRACT\|TYPE_DIRECT)`，成功才认；`c6_tuple_get` 校验元素并规范化出内部 tag `__c6_tuple_N`；`c6_tuple_of_type` / `c6_tuple_ctype` 查询 | `tcc/tccgen.c` |
| `t[k]` 元素访问 | `c6_tuple_subscript`（`unary()` 的 `[` 后置分支，先于切片下标） | `tcc/tccgen.c` |
| 元组强转 / 复合字面量 / `sizeof` / `_Alignof` | `unary()` 的 `case '('` 元组分支（`c6_tuple_ate_paren` + `wrap` 处理多包一层括号的情形） | `tcc/tccgen.c` |
| 解构声明 / 解构赋值 | `c6_tuple_lead_kind`（分类：表达式/类型/模式/赋值，全部靠 token 重放，失败即回滚）、`c6_tuple_destructure`（溢到隐藏局部再逐元素赋值，并重放语句） | `tcc/tccgen.c` |
| `return { … };` | `c6_return_value`（普通函数与 `async` 函数共用） | `tcc/tccgen.c` |
| 元组的类型回放 | `c6_async_type_tokens` 对元组输出 `(T1, T2)`（不复用内部 tag 名，否则跳板里会声明出一个作用域不同的新 struct） | `tcc/tccgen.c` |
| `!error` 的聚合返回值 | `c6_apply_fallible`（白名单加 `VT_STRUCT` / `VT_UNION`）、`c6_zero_local`、`c6_emit_propagate` / `c6_async_propagate`（失败路径清零） | `tcc/tccgen.c` |
| 函数字面量 `RET (参数) { 体 }` | `C6Lit` 队列 + `c6_lit_lead()`（读前缀 → 扫描签名 → token 重放判定「函数 + `{`」）、`c6_lit_parse()`（建 `static` 符号、`skip_or_save_block` 存体）、`c6_lit_decl_lead()` / `c6_lit_infer_decl()`（`f = …;` 推断声明）、`c6_lit_flush()`（TU 末尾 `gen_function()`）、`c6_lit_cap_check()`（捕获诊断，挂在值/typedef/tag 三条符号解析路径上）；`unary()` 的 `tok_next`、`case '('` 与 `block()` 的语句入口各一处钩子 | `tcc/tccgen.c` |
| 切片下标 / 区间 | `c6_slice_subscript`（`unary()` 的 `[` 分支）、`c6_slice_spill` | `tcc/tccgen.c` |
| 数组 → 切片 | `c6_slice_convert`（`vstore` / `gen_assign_cast` / 强转 / `decl_initializer`） | `tcc/tccgen.c` |
| `@T` / `shared @T` / `weak @T` / `own @T` 限定符 | `parse_btype()` 的 `@` 前缀 + `c6_gc_qual()`（软关键字 `weak`/`own`/`shared`）、`CType.gq` 缓存 + pointee 上的可信副本、`c6_type_gq` / `c6_is_managed` / `c6_gc_plain` / `c6_gc_mode` | `tcc/tccgen.c`、`tcc/tcc.h` |
| `new T` / `new T[n]` / `new shared T` | `c6_gc_new_expr()`（`unary()` 的 `c6_gc_new_lead()` 分支）→ `__c6_gc_new(size, count, desc, flags)` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| 托管槽的存/取 | `c6_gc_store_hook()`（`vstore()` 里）→ `__c6_gc_store` / `__c6_gc_own_store` / `__c6_gc_weak_store` | 同上 |
| 作用域退出 / `return` / `break` / 跳转 | `c6_gc_site()`（合成一个清理点）+ `__c6_gc_release(slot, mode)` | 同上 |
| `p = NULL` 提前放手 | 同一次 `__c6_gc_store` 调用（旧值释放、槽清零） | 同上 |
| `move(p)` | `c6_gc_move_expr()`（`unary()` 的 `c6_gc_move_lead()` 分支）→ `__c6_gc_move(slot)` | 同上 |
| `move(p)` 之后、重新赋值之前用 `p` | 编译期诊断 E-GC17（不生成代码）：标记在 `Sym.gc_moved`，`unary()` 的标识符分支调 `c6_gc_moved_use()`；赋值清标记在 `c6_gc_store_hook()`；分支合并 `c6_move_snap` / `c6_move_restore` / `c6_move_merge` / `c6_move_branch_end`（`block()` 的 `if`/`while`/`for`/`switch`、`c6_foreach_parse`），`defer`/`errdefer` 体与 `catch` 体各用一次快照隔离（`c6_defer_parse()` / `c6_catch_parse()`） | 同上 |
| 描述符（字段表 + 析构指针） | `c6_gc_collect()` / `c6_gc_flush_descs()`（TU 末尾发射，名字带文件名哈希） | `tcc/tccgen.c` |
| 环检测 | `c6_gc_cycle_check()` / `c6_gc_cycle_dfs()`（`c6_gc_flush_descs()` 里） | `tcc/tccgen.c` |
| 析构 `drop` | `c6_gc_drop_register()`（`c6_method_register()` 里）、`c6_gc_is_drop()` / `c6_gc_drop_callee()`（禁止显式调用）、描述符的 `drop` 指针 | 同上 |
| 全局根 | `c6_gc_root_add()`（`decl()` 里）+ TU 末尾的 `__attribute__((constructor))` / `((destructor))` 对 | 同上 |
| 运行期全部 | `__c6_gc_new` / `__c6_gc_store` / `__c6_gc_release` / `__c6_gc_drop` / `__c6_gc_move` / `__c6_gc_incref` / `__c6_gc_weak_store` / `__c6_gc_drain_budget` / `__c6_gc_shutdown`，以及 `async`/`go` 的任务槽归还（`__c6_rel_NAME`）| `tcc/lib/libtcc1.c`、`tcc/c99-rt.inc` |
| `foreach` | `c6_foreach_parse`（`block()` 的 `TOK_C6_FOREACH` 分支；`tok != '\|'` 时 `unget_tok` 退回表达式语句）、`c6_common_int_type` / `c6_is_int_type`、`c6_iter_proto_of` / `c6_iter_setup` / `c6_iter_call`（迭代器协议：结构化识别、静态调用或 vtable 分派）；数组/切片的隐藏槽复用 `c6_slice_spill`，循环体走 `lblock`；文件作用域诊断在 `decl()` | `tcc/tccgen.c` |
| 切片运行期 | `__tcc_slice_index` / `__tcc_slice_sub` | `tcc/lib/libtcc1.c` |
| `async` 声明 | `c6_async_declare`（合成 `struct NAME_task` 的 token）、`FuncAttr.func_async`、`parse_btype()` 的 `TOK_C6_ASYNC` | `tcc/tccgen.c`、`tcc/tcc.h` |
| `async` 定义 | `c6_async_definition` / `c6_async_func_begin` / `c6_async_params_copy` / `c6_async_func_end`（恢复分发器）、`c6_async_finish` | `tcc/tccgen.c` |
| 调用改写 | `c6_async_call`（`unary()` 的 `tok_identifier` 分支）+ `c6_async_push_desc` | `tcc/tccgen.c` |
| `await` | `c6_await_parse`（`unary()` 前缀分支）、`c6_async_suspend`、`c6_async_await_task`、`c6_async_await_io` | `tcc/tccgen.c` |
| 协程运行期 | `__c6_co_new` / `__c6_co_await` / `__c6_co_suspend` / `__c6_co_restore` / `__c6_co_finish` / `__c6_co_wait` / `__c6_co_destroy` | `tcc/lib/libtcc1.c` |
| 预置声明 | `tcc_predefs()` 的 `__TCC_ASYNC_DEFINED` 段 | `tcc/tccpp.c` |
关键字表：`tcc/tcctok.h` 末尾的 `TOK_C6_*` 追加项。改动词法器时
注意它们必须留在文件的**最后**，否则会破坏 i386 内联汇编的
`TOK_ASM_first/last/alllast` 区间。

编译器内部的错误槽接口（由 `lib/libtcc1.c` 提供）：

```
int         __tcc_err_flag_get(void);
int         __tcc_err_id_get(void);
const char *__tcc_err_msg_get(void);
void        __tcc_err_clear(void);
void        __tcc_err_set(int id, const char *msg);
void        __tcc_err_flag_peek(int *out);   (* 编译器内部使用 *)
void        __tcc_error_take(void *out);     (* 快照并清零错误槽 *)
```
