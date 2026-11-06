# c6 语言扩展语法规范（BNF / EBNF）

本文件用 **ISO/IEC 14977 扩展巴科斯范式（EBNF）**给出 TinyCC 的 c6
扩展相对 ISO C 的**增量语法**。标准 C 的语法不再重复；文中出现的
`declaration`、`statement`、`expression` 等未定义非终结符一律指 ISO C
（C11 / N1570）中的同名产生式。

扩展只在编译期定义了 `C6_EXT` 且命令行给了 `-std=c6` 时生效；此时
预处理器额外定义宏 `__C6__`。未开启时，下列新关键字全部退化为普通
标识符，本文档描述的产生式一条都不适用。

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
c6-soft-keyword = "co" | "go" | "task" ;

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

top-level-declaration  = package-declaration
                       | import-declaration
                       | interface-declaration
                       | test-declaration
                       | declaration ;

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
                         (* 打开 'pkg.member' 写法，必须出现在使用点之前 *)

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

约束 C19 要求**每个变量定义都带初始化器**：

```
    int g = 0;                      (* 文件作用域的定义也要写 *)
    static int counter = 0;

    void f(void)
    {
        int i = 0;
        int a[3] = {1, 2, 3};
        struct S s = {0};
        static int calls = 0;
        extern int declared_elsewhere;   (* 合法：声明，不是定义 *)
        int vla[n];                      (* 合法：VLA 是唯一豁免 *)
    }
```

规则在**定义点**检查，不做数据流分析（`int x; x = 1;` 一样报错）。
下列构造不是“变量定义”，因此不受约束：`extern` 声明、函数形参（含 K&R
形参声明表）、结构体/联合体成员、`typedef`、函数声明与定义。
VLA 是唯一例外——标准 C 不允许给变长数组写初始化器，所以只能豁免。

诊断里使用源码中书写的标识符：`package` 内的变量报原名，而不是
`NAME__x`。

这条约束只作用于开启了 `-std=c6` 的翻译单元：不带该开关的老代码
完全不受影响；一旦开启，该 TU 里出现的所有定义（含包含进来的头文件里的
函数体）都要满足。K&R 老式定义的形参声明表属于形参，不受约束。

### 3.2 结构体方法

文件作用域的函数声明或定义，只要**第一个参数是指向结构体（或联合体）的
指针**，就同时注册为该类型的方法：

```
    struct Vec { int x, y; };

    int vec_sum(struct Vec *self) { return self->x + self->y; }

    void f(struct Vec *p) { p->vec_sum(); }   (* 等价于 vec_sum(p) *)
```

方法**不是**新的函数类型：符号名和调用约定都不变，`vec_sum(p)` 与
`p->vec_sum()` 等价。约束见 C20。

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

运行时接口（`-std=c6` 时由 `__TCC_ASYNC_DEFINED` 预置声明）：

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
* 每个线程有自己的 defer 栈、错误槽和协程池（`pthread_key`，不是
  `__thread`：`-run` 不支持 TLS），所以线程里可以照常写 `defer`、`err`、
  `async`/`await`，也可以再 `go`。

运行时接口（`-std=c6` 时预置声明）：

```
void *__tcc_sp_args(unsigned long size);   (* 参数块 *)
int   __tcc_th_start(void *(*fn)(void *), void *arg);   (* 起线程，0 = 成功 *)
void  __c6_th_join_all(void);              (* 等本线程 go 出去的线程 *)
```

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
* 只在 `-std=c6` 的翻译单元里生效；`struct Vec v;` 永远可用。

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
                       | foreach-statement ;

defer-statement        = "defer" , statement ;
                         (* 注册时跳过；离开作用域或返回/重抛时按注册的
                            逆序执行 *)

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
```

`foreach` 的三种可迭代对象与绑定数量一一对应：

| 可迭代对象 | 绑定 | 下标类型 | 值绑定 |
|---|---|---|---|
| 完整数组 `E[N]`（含字符串字面量） | `\|i, v\|` | `size_t` | 元素的一份副本 |
| 切片 `E[:]` | `\|i, v\|` | `size_t` | 元素的一份副本 |
| 整数区间 `lo:hi` | `\|i\|` | 两个界的公共整数类型（至少 `int`） | 无 |

语义要点：

- 可迭代对象**只求值一次**：数组/切片落到一个隐藏的
  `{ E *data; size_t len }` 帧槽（与切片的运行期布局相同，所以
  `foreach |i, v| obj.items` 直接可用），区间的上界落到一个隐藏的整数槽；
- 数组每轮重新按 `data[i]` 取元素（数组可能在循环体里被改），切片用求值
  那一刻的 `{data, len}` 快照；
- 值绑定是**副本**：写它不改变容器。下标绑定就是循环变量，写它会改变
  后续迭代（与 `for (i = 0; i < n; i++)` 完全一样）；
- `lo >= hi` 是零次迭代，两个界各只求值一次；
- `break`、`continue`、`defer`、`errdefer`、`await`、`goto` 与写在 `for`
  体里完全一致（循环体是普通复合语句，`break` 只跳出一层）；
- 循环体**必须**是复合语句（花括号不能省），绑定的作用域就是这条语句；
- 元素类型必须是可赋值的完整对象类型：元素是数组时先 `typedef` 起名
  （`typedef int Row[3]; foreach |i, r| rows { … }`）；
- VLA、不完整数组、指针、整数、没有暴露切片的其它结构体都不可迭代
  （见第 10 节）。

---

## 5. 表达式

```
primary-expression     = …
                       | qualified-identifier
                       | try-expression
                       | await-expression ;

await-expression       = "await" , unary-expression ;
                         (* 操作数最终必须落到一次 async 函数调用或
                            c6_io(fd, ev) 上，见 C30 *)

qualified-identifier   = identifier , "." , identifier ,
                         { "." , identifier } ;
                         (* 'pkg.member'：第一段必须是已 import 的包名，
                            否则按普通 C 的成员访问解析；见 C14/R7 *)

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
| C9 | `!error` 的函数返回类型必须能表示哨兵 `-1`（标量或枚举） | `'!error' requires a return type representable by -1` |
| C10 | `expr` 中不能直接写裸 `catch` | 同 C7 |
| C11 | `try` / `catch` / `err` 不得出现在 `defer` / `errdefer` 体内 | `'try' is not allowed in a defer body` 等 |
| C12 | 返回类型大小未知（不完整类型）的函数不能传播错误 | `cannot propagate an error from a function with an incomplete return type` |
| C13 | `package` 内的符号被改写为 `NAME__sym`，`main` 除外；嵌套与点号形式组合前缀 | — |
| C14 | `pkg.member` 只在 `pkg` 已 `import`（或就是当前包）时成立 | `'pkg' is not an imported package (use 'import pkg;')` |
| C15 | `T!error` 调用的结果必须被 `try` 或 `catch` 消费（`sizeof` / `typeof` 的操作数不产生调用，不受此限） | `the result of a 'T!error' function must be used with 'try' or 'catch'` |
| C16 | `err` 必须带 `id` 与 `msg` 两个实参（没有重抛形式） | `'err' requires an error id and a message` |
| C17 | `err` 只能出现在返回 `T!error` 的函数里 | `'err' requires the enclosing function to return 'T!error'` |
| C18 | `error` 只能作为返回值，不能作为形参（含 `error*` / `error[]`，K&R 形参声明也算） | `'error' can only be a return type, not a parameter` |
| C19 | 每个变量定义都必须带初始化器（`extern` 声明、形参、成员、`typedef` 除外；VLA 豁免） | `variable 'x' must be initialized` |
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
| C44 | 数组/切片要两个绑定 | `an array or slice yields an index and a value: write 'foreach \|i, v\| a'` |
| C45 | 区间只要一个绑定 | `a range yields only an index: write 'foreach \|i\| lo:hi'` |
| C46 | 两个绑定不能都丢弃 | `'foreach' discards both binders` |
| C47 | 可迭代对象只能是完整数组、切片或整数区间 | `cannot iterate over a variable length array`；`cannot iterate over an array of unknown length`；`cannot iterate over 'T *' (use a slice parameter 'T[:]', or '(T[:]){p, n}')`；`cannot iterate over 'int' (write a range: 'foreach \|i\| lo:hi')`；`cannot iterate over 'T' (expose a slice member, e.g. 'T[:] items')` |
| C48 | 区间两个界必须是整数 | `range bounds must be integers` |
| C49 | 元素类型必须可赋值且完整 | `'foreach' cannot copy an element of type 'T'`；`cannot iterate over incomplete element type 'T'` |
| C50 | 循环体必须是复合语句 | `'{' expected` |

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
    f(p, args)                   (* 同一次调用，符号名与调用约定都不变 *)
```

接收者先求值并存入一个临时槽，再作为第一个实参压栈：若方法返回结构体，
隐藏的 `self` 排在 sret 缓冲之后。

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
```

生成的代码就是上面那段 `for`（同一套 `lblock` / `gvtst` / `gjmp_addr`），
没有运行期辅助函数、没有虚调用、没有迭代器对象：数组/切片的隐藏槽就是
`c6_slice_spill` 用的 `{data, len}` 对，所以 `foreach |i, v| f()`（`f`
返回切片）与 `foreach |i, v| obj.items` 走同一条路径。`_` 绑定时对应的
加载/存储整段不生成。

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

/* 方法名共享 C 的全局命名空间：不同类型要用同名方法，就放进不同包 */
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

**R14 — `async T f(P…) { body }`**

```
int __c6_body_f(void *task);
const struct __c6_desc __c6_desc_f = { sizeof(struct f_task), alignof, 64, __c6_body_f };
```

函数体按 `int __c6_body_f(void *task)` 编译。序言先算帧基址
`base = &marker - off(marker)`，再看 `__c6_co_state(task)`：非 0 就把保存
的帧拷回 `base - size` 并跳到恢复分发器，为 0 就正常往下执行。声明的形参
在此时被声明成普通局部变量，并从 `task->pN` 拷进来（只有首次进入会走这条
路径）。

`struct f_task` 由 `c6_async_declare()` 用合成 token 在文件作用域声明，
所以同一个 `async` 声明在定义方和调用方得到同一个布局。

**R15 — `await`**

```
    x = await f(a)          ⇒  task = __c6_co_new(&__c6_desc_f);
                               task->p0 = a;
                               if (__c6_co_await(task, self))
                                   __c6_co_suspend(self, KIND_CHILD, N, base, size, defer_base);
                                   return KIND_CHILD;     (* 回到驱动方 *)
                               N:  x = task->val;          (* 恢复后从这里继续 *)

    await c6_io(fd, ev)     ⇒  __c6_co_set_io(self, fd, ev);
                               __c6_co_suspend(self, KIND_IO, N, base, size, defer_base);
                               return KIND_IO;
                               N:  __c6_co_rev(self);      (* 就绪掩码 *)

    return e;               ⇒  self->val = e;
                               <执行 defer 链>
                               __c6_co_finish(self, KIND_DONE);
                               return KIND_DONE;
```

挂起时把 `[base - size, base)` 之间的原生帧、`[defer_base, defer_sp)` 的
defer 记录、以及错误槽一起存进任务；恢复时原样拷回，再跳到该号恢复点。
因此**局部变量、defer 记录、错误状态在挂起前后完全不变**。

驱动方 `__c6_co_wait(task)` 反复进入 `__c6_body_f`：每次拿到的 `kind` 是
`DONE` / `IO` / `CHILD`，为 `CHILD` 时先往子任务里钻，为 `IO` 时用
`select()` 等 fd 就绪并把掩码放进 `wait_rev`。

**R16 — `co f(a);` / `task t = f(a);`**

```
    co f(a)                 ⇒  h = __c6_co_new(&__c6_desc_f);   (* 同 R14 的调用 *)
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
结束的只把计数留给下一次 `__c6_co_run()`。`task` 的清理登记复用
`__tcc_defer_push`，因此和手写 `defer { __c6_co_destroy(t); }` 等价。

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

int main(void)
{
    int a[5] = {1, 2, 3, 4, 5};
    int[:] s = a[1:4];                      /* 2 3 4 */
    struct Pair pairs[2] = {{1, 2}, {3, 4}};
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
    return total == 42 ? 0 : 1;             /* 15 + 9 + 3 + 15 */
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

完整可运行的版本见 `examples/async_demo.c`（用 `pipe()` 代替 socket，
离线就能跑）；自测见 `tests/async.c`。

## 10. 未支持的写法

以下写法**不是**本规范的一部分，编译器会报错：

| 写法 | 结果 |
|---|---|
| `try { … } catch { … }` 块形式 | 语法错误（`try` 只接一次调用） |
| `try a = f();` | 语法错误（`try` 只接调用表达式） |
| `catch` 处理块自然落到末尾 | 约束 C6 |
| `int v = 0; try v;` | 约束 C1/C2 |
| `struct S!error f(void)` | 约束 C9（结构体无法用 `-1` 表示哨兵） |
| `int!error x;` | 约束 C8 |
| `try` / `catch` / `err` 写在 `defer` 体内 | 约束 C11 |
| `err();`（无参数重抛） | 约束 C16，请改用 `try` |
| `void f(error e);` / `void f(error *e);` | 约束 C18：错误只从返回值出来 |
| `f() catch e;`（无处理块） | 语法错误 |
| `catch e { … }` 单独出现 | 约束 C7 |
| `int x;` / `static int x;` / `int a[3];`（不带初始化器的定义） | 约束 C19，VLA 除外 |
| `Vec { int x, y; };`（定义处省略 `struct`） | 仍要写 `struct`：裸 `TAG { … }` 分不清 struct/union，也容易和初始化器混淆 |
| `stat st;`（tag 名被函数/变量遮蔽） | 约束 C41：按普通 C 解析，需要类型时写 `struct stat` |
| `S *p;` 而 `S` 未声明 | 约束 C41：不会隐式前置声明 tag，报 `'S' undeclared` |
| `int x = Vec;`（tag 当值用） | tag 只在类型位置生效：`'Vec' undeclared` |
| `math.add(1, 2)` 而此前没有 `import math;` | 约束 C14 |
| `impl Shape for Circle { … }` | `impl` 不再是关键字，按普通声明解析而报错；改用「结构体拥有方法」实现接口 |
| `v.f()`（值是结构体而不是指针） | 按普通成员访问报 `field not found: f`；方法调用只支持 `->` |
| `p->f`（方法不带调用） | 约束 C20：`method 'f' must be called` |
| 方法在使用点之后才声明 | 约束 C20/C21（TCC 单趟编译） |
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
| 同时存活超过 64 个协程 | 固定池（`C6_CO_MAX`）：超了会 trap，不是语法错误 |
| `await c6_io(fd, ev)` 的 `fd` 不在 `[0, 1024)` | 任务按失败回收：`__c6_co_wait` 返回 `-1`，`__c6_co_run` 计一次失败（不会永远 park） |
| 抢占式调度、把阻塞的 libc 调用（`read`/`printf`/…）自动变成让出点 | 不在本扩展范围：`co`/`__c6_co_run` 只推进协程，不 hook libc |
| PE 目标上的 `go` | 还没有 PE 线程支持，运行时 trap |
| 取消 / 强杀一个 `go` 出去的线程 | 不提供：线程总是被启动它的线程 join |
| `for a \|i, v\| { }` / `for 0..n` | `for` 保持纯 C；迭代请用 `foreach` |
| `foreach a { }`（不写绑定表） | 语法错误：绑定表必须紧跟 `foreach` |
| `foreach (a) \|i, v\| { }`（括号形式） | 不支持：`foreach` 后面直接跟绑定表 |
| `foreach \|i, v\| a v = 1;`（没有花括号） | 约束 C50 |
| 文件作用域的 `foreach \|i, v\| a { }` | 约束 C42 |
| `foreach \|i, v\| p { }`（`p` 是指针） | 约束 C47：先写 `(T[:]){p, n}` |
| `foreach \|i, v\| 5 { }`（整数） | 约束 C47：整数要先写成区间 `0:5` |
| `foreach \|i, v\| s { }`（`s` 是没暴露切片的 `struct`） | 约束 C47：用类型暴露的切片成员，例如 `foreach \|i, v\| s.items` |
| `foreach \|i, v\| vla { }` | 约束 C47：VLA 的长度运行期才知道 |
| `foreach \|_, _\| a { }` | 约束 C46：两个绑定都丢弃没有意义 |
| `foreach \|i\| a { }` / `foreach \|i, v\| 0:n { }` | 约束 C44/C45：绑定数量由可迭代对象决定 |
| `interface iterator { bool has(); void *next(); }` + `for a \|i, v\| { }` | v1 不做运行期迭代协议：`void *next()` 会擦掉元素类型（每轮都要装箱/分配），而方法名不按接收者改名，`has`/`len`/`at` 会和用户函数撞名。可迭代对象限定为数组、切片和区间，其它类型请暴露一个切片成员 |
| `0..count`（点号区间） | 词法上 `0..3` 是 `0.` 与 `.3` 两个浮点常量，改词法会波及现有代码；统一写 `0:count` |
---

## 11. 实现位置

| 语法 | 实现 | 位置 |
|---|---|---|
| `package` / `import` | `c6_package_parse` / `c6_import_parse` / `c6_pkg_name` / `c6_pkg_compose` / `c6_pkg_imported`，`unary()` 里的点号解析 | `tcc/tccgen.c` |
| `pkg.member` | `unary()` 的 `tok_identifier` 分支（逐段增长前缀 + `sym_find_raw`） | `tcc/tccgen.c` |
| 结构体方法 | `c6_method_register`（在 `decl()` 里注册）/ `c6_method_lookup` / `c6_has_field` / `c6_method_call_prep`，`unary()` 的 `->` 分支 | `tcc/tccgen.c` |
| `interface` | `c6_interface_parse`、`c6_auto_impl_ensure` / `c6_check_method` / `c6_emit_auto_impls` / `c6_iface_convert` | `tcc/tccgen.c` |
| 每 TU 状态复位 | `c6_tu_reset`（在 `tccgen_compile` 开头调用） | `tcc/tccgen.c` |
| `test` | `c6_test_parse`、`tcc_run_tests` | `tcc/tccgen.c`、`tcc/tccrun.c` |
| `defer` / `errdefer` | `c6_defer_parse`、`c6_defer_begin`、`c6_defer_func_end` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| `err` | `c6_err_parse` / `c6_emit_propagate` | `tcc/tccgen.c` |
| `co` | `c6_co_parse`、`__tcc_co_spawn` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| `go` | `c6_go_parse` / `c6_sp_emit` / `c6_sp_flush`、`__tcc_sp_args` / `__tcc_th_start` / `__c6_th_join_all` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| 软关键字 | `c6_soft_kw` / `c6_soft_kw_shadowed`（`block()` 与 `decl()` 两个入口） | `tcc/tccgen.c` |
| 每线程状态 | `tcc_th()` / `tcc_th_join_children`（`pthread_key`） | `tcc/lib/libtcc1.c` |
| `task` | `c6_task_decl` / `c6_task_check_handle` / `c6_task_defer_destroy` | `tcc/tccgen.c` |
| 顶层调度（64 槽就绪链表 + 一次 `select()`） | `tcc_co_pump` / `tcc_co_step_once` / `tcc_co_link` / `tcc_co_unlink` / `__c6_co_run` | `tcc/lib/libtcc1.c` |
| `!error` | `c6_parse_fallible_suffix` / `c6_apply_fallible`，`FuncAttr.func_fallible` | `tcc/tccgen.c`、`tcc/tcc.h` |
| `try` | `c6_try_parse`（`unary()` 的前缀分支） | `tcc/tccgen.c` |
| `catch` | `c6_catch_parse`（`expr_catch()` 层，位于 `expr_cond()` 之下） | `tcc/tccgen.c` |
| `#macro` / `__local` | `parse_c6_macro` / `c6_hygiene_body` | `tcc/tccpp.c` |
| 变量初始化 | `c6_check_var_initialized`（在 `decl()` 的对象定义分支调用） | `tcc/tccgen.c` |
| 裸 tag 类型名 | `parse_btype()` 的 `default` 分支：`sym_find()` 未命中时试 `struct_find()`，命中则走 `basic_type2`（与 `struct TAG` 同一条路径）；`ignore_label` 时 `TAG:` 退回标签 | `tcc/tccgen.c` |
| `T[:]` 类型 | `parse_btype()` 的 `case '['`、`c6_slice_get`（按元素类型规范化生成内部 struct） | `tcc/tccgen.c` |
| 切片下标 / 区间 | `c6_slice_subscript`（`unary()` 的 `[` 分支）、`c6_slice_spill` | `tcc/tccgen.c` |
| 数组 → 切片 | `c6_slice_convert`（`vstore` / `gen_assign_cast` / 强转 / `decl_initializer`） | `tcc/tccgen.c` |
| `foreach` | `c6_foreach_parse`（`block()` 的 `TOK_C6_FOREACH` 分支；`tok != '\|'` 时 `unget_tok` 退回表达式语句）、`c6_common_int_type` / `c6_is_int_type`；数组/切片的隐藏槽复用 `c6_slice_spill`，循环体走 `lblock`；文件作用域诊断在 `decl()` | `tcc/tccgen.c` |
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
