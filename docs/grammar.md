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
    | "try"     | "catch" ;

(* 扩展内建的错误值类型，由编译器预置的 typedef 提供，等价于
   typedef struct __tcc_error { int id; const char *msg; } error;
   'error' 只能出现在返回类型里（作为 'T!error' 的错误载荷），
   不能作为函数参数，见约束 C18。 *)
error-type-name = "error" ;
```

切片类型 `T[:]` 不引入关键字：它由已有的 `[`、`:`、`]` 三个记号组成，
只在「声明说明符之后」这个 C 里本来非法的位置被识别（见 3.3）。

`try`、`catch`、`err`、`defer`、`errdefer`、`package`、`import`、
`interface` 只会作为关键字出现在下列产生式允许的位置；在其余
位置（例如声明说明符中）它们仍可被当作标识符使用，这一点与 C 关键字
不同，是单趟实现的已知宽松之处。`impl` 自 0010 起不再是关键字。

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
                       | try-statement ;

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
```

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
`__tcc_slice_*`，它执行一次故意的空指针写。libtcc1 是 freestanding 的
（没有 libc），因此不产生诊断文本，与 `catch` 落空时一致。

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
| `!error` | `c6_parse_fallible_suffix` / `c6_apply_fallible`，`FuncAttr.func_fallible` | `tcc/tccgen.c`、`tcc/tcc.h` |
| `try` | `c6_try_parse`（`unary()` 的前缀分支） | `tcc/tccgen.c` |
| `catch` | `c6_catch_parse`（`expr_catch()` 层，位于 `expr_cond()` 之下） | `tcc/tccgen.c` |
| `#macro` / `__local` | `parse_c6_macro` / `c6_hygiene_body` | `tcc/tccpp.c` |
| 变量初始化 | `c6_check_var_initialized`（在 `decl()` 的对象定义分支调用） | `tcc/tccgen.c` |
| `T[:]` 类型 | `parse_btype()` 的 `case '['`、`c6_slice_get`（按元素类型规范化生成内部 struct） | `tcc/tccgen.c` |
| 切片下标 / 区间 | `c6_slice_subscript`（`unary()` 的 `[` 分支）、`c6_slice_spill` | `tcc/tccgen.c` |
| 数组 → 切片 | `c6_slice_convert`（`vstore` / `gen_assign_cast` / 强转 / `decl_initializer`） | `tcc/tccgen.c` |
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
