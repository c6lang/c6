# superc 语言扩展语法规范（BNF / EBNF）

本文件用 **ISO/IEC 14977 扩展巴科斯范式（EBNF）**给出 TinyCC 的 superc
扩展相对 ISO C 的**增量语法**。标准 C 的语法不再重复；文中出现的
`declaration`、`statement`、`expression` 等未定义非终结符一律指 ISO C
（C11 / N1570）中的同名产生式。

扩展只在编译期定义了 `TCC_EXT` 且命令行给了 `-std=tcc-ext` 时生效；此时
预处理器额外定义宏 `__TCC_EXT__`。未开启时，下列新关键字全部退化为普通
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
superc-keyword =
      "package" | "import" | "interface"
    | "defer"   | "errdefer" | "err" | "test"
    | "try"     | "catch" ;

(* 扩展内建的错误值类型，由编译器预置的 typedef 提供，等价于
   typedef struct __tcc_error { int id; const char *msg; } error;
   'error' 只能出现在返回类型里（作为 'T!error' 的错误载荷），
   不能作为函数参数，见约束 C18。 *)
error-type-name = "error" ;
```

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

这条约束只作用于开启了 `-std=tcc-ext` 的翻译单元：不带该开关的老代码
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
                       | try-expression ;

qualified-identifier   = identifier , "." , identifier ,
                         { "." , identifier } ;
                         (* 'pkg.member'：第一段必须是已 import 的包名，
                            否则按普通 C 的成员访问解析；见 C14/R7 *)

postfix-expression     = …
                       | postfix-expression , "->" , identifier ,
                         "(" , [ argument-expression-list ] , ")"
                         (* 方法调用：接收者须为指向结构体/联合体的指针，
                            且该类型有同名方法、没有同名字段；见 C20/R8 *)
                       | postfix-expression , catch-clause ;

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
superc-directive       = "#" , "macro" , identifier ,
                         [ "(" , [ parameter-list ] , ")" ] ,
                         "{" , { preprocessing-token } , "}" ;

parameter-list         = identifier , { "," , identifier } ;
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

---

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

---

## 11. 实现位置

| 语法 | 实现 | 位置 |
|---|---|---|
| `package` / `import` | `superc_package_parse` / `superc_import_parse` / `superc_pkg_name` / `superc_pkg_compose` / `superc_pkg_imported`，`unary()` 里的点号解析 | `tcc/tccgen.c` |
| `pkg.member` | `unary()` 的 `tok_identifier` 分支（逐段增长前缀 + `sym_find_raw`） | `tcc/tccgen.c` |
| 结构体方法 | `superc_method_register`（在 `decl()` 里注册）/ `superc_method_lookup` / `superc_has_field` / `superc_method_call_prep`，`unary()` 的 `->` 分支 | `tcc/tccgen.c` |
| `interface` | `superc_interface_parse`、`superc_auto_impl_ensure` / `superc_check_method` / `superc_emit_auto_impls` / `superc_iface_convert` | `tcc/tccgen.c` |
| 每 TU 状态复位 | `superc_tu_reset`（在 `tccgen_compile` 开头调用） | `tcc/tccgen.c` |
| `test` | `superc_test_parse`、`tcc_run_tests` | `tcc/tccgen.c`、`tcc/tccrun.c` |
| `defer` / `errdefer` | `superc_defer_parse`、`superc_defer_begin`、`superc_defer_func_end` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| `err` | `superc_err_parse` / `superc_emit_propagate` | `tcc/tccgen.c` |
| `!error` | `superc_parse_fallible_suffix` / `superc_apply_fallible`，`FuncAttr.func_fallible` | `tcc/tccgen.c`、`tcc/tcc.h` |
| `try` | `superc_try_parse`（`unary()` 的前缀分支） | `tcc/tccgen.c` |
| `catch` | `superc_catch_parse`（`expr_catch()` 层，位于 `expr_cond()` 之下） | `tcc/tccgen.c` |
| `#macro` / `__local` | `parse_superc_macro` / `superc_hygiene_body` | `tcc/tccpp.c` |
| 变量初始化 | `superc_check_var_initialized`（在 `decl()` 的对象定义分支调用） | `tcc/tccgen.c` |

关键字表：`tcc/tcctok.h` 末尾的 `TOK_SUPERC_*` 追加项。改动词法器时
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
