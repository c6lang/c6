# c6 泛型设计（`@` 应用 / `|…|` 绑定）

> **状态**：已落地（阶段 1–3 全部完成） · **索引**：[文档索引](../README.md) · **语法**：[grammar.md §3.11](../reference/grammar.md) · **诊断**：[diagnostics.md](../reference/diagnostics.md)（C103–C116）

状态：**阶段 1–3 已全部落地**（模板/实例、`|…|` 函数与 typedef 模板、值形参、
体内成员函数 V3、模块前缀与实例名冲突检查、`-Wgeneric-depth`），`.c6` 文件里
可用。能写的：`struct Vect@T {…}`
/ `struct Map@(K, V) {…}` 模板声明，`Vect@int` / `Map@(char[:], int)` 应用，
嵌套（`Vect@Vect@int`）与自引用，`typedef` 别名，实例上的方法，切片后缀
（`Vect@int[:]`）；函数/typedef 模板用前缀 `|T|` 绑定
（`|T| T vat(Vect@T *self, int i)`、`|T| typedef Vect@T MyVec`），一份模板
覆盖所有实例，显式应用 `vat@int(v, 0)` 与接收者写法 `p->vat(0)` 等价。
实例 mangle 成 `Vect_int`，所以它就是一个普通 struct、普通函数——方法符号
`Vect_int_<name>`，实例函数 `Vect_int_vat`，纯 C 的 TU 写 `struct Vect_int`
即可互通（`tests/abi/generic.c6`）。值形参写成 C 参数表形状
（`struct Buf@(T, int N)`，实参 `Buf@(int, 4)`）并按常量绑定。**体内成员函数**
（§8.5）也可以写了：`struct Vect@T { T *data; int len; T at(int i) { return
data[i]; } }` —— 方法体里接收者就是隐式的 `self`，字段和方法按名字用，符号仍是
`Vect_int_at`，所以 `|T|` 前缀写法、体内写法、纯 C 三者是同一套 ABI。
`-emit-header` 已经输出实例（`struct pkg__Vect_int` + 方法原型），模块前缀与
实例名冲突检查也都在（§9 阶段 2）。

相关：[grammar.md](../reference/grammar.md)（§3.2 方法、§3.3 切片 `T[:]`、§3.7 裸 tag 类型名 C41、
§3.8 元组、§7 静态约束、implementation-notes.md 的实现位置表）、[closure-design.md](closure-design.md)
（V2 闭包，`|…|` 记号的另一个用处）、[gc-design.md](gc-design.md)（`@T` 的计数与环检测）。

---

## 0. 决策速览

| 编号 | 决策 | 理由（详见 §1、§2） |
|---|---|---|
| D1 | 类型应用写作 `Name@arg`，**后缀、无闭合括号** | `Vect@Vect@int` 线性展开，永远不出现 `>>>` / `]]]` 的视觉坍塌；`>` 在表达式里是合法运算符，`@` 在类型名之后不是 |
| D2 | tag 定义的形参挂名：`struct Vect@T { … }` | 形参与实参同形（Rust/Go/C#/Swift 的主流直觉）；此处名字先于体，形参可见 |
| D3 | 名字出现在**体或返回类型之后**的声明（函数、typedef）用前缀 `\|T\|` | 由 A3「参数必须先于使用」推导，不是风格选择：C 的返回类型在名字左边，挂名形式永远看不到 `T` |
| D4 | `@` 与既有托管引用 `@T` 共存 | 前缀 `@`（类型位置起点）= 托管引用；后缀 `@`（类型名之后）= 类型应用；位置不同，零歧义 |
| D5 | 单参数省略括号，多参数必须括号；声明点与使用点**同一条括号规则**；值实参一律带括号 | `struct Map@(K, V)` / `Map@(char[:], int)`；常量表达式不会像类型那样自己停下来，`Arr@(4)` |
| D6 | 单态化：一个实例 = 一个独立 struct tag + 一批 `<mangled>_<method>` 函数；模板本体永不完整 | 实例走 C 的 ABI，五个后端 + c99 后端 + `-emit-header` 零改动 |
| D7 | 值形参写成 C 参数表形状 `int N`，内部按普通**常量符号**绑定（`VT_CONST`） | `N` 天然可进常量表达式（数组长度、切片长度、`case`、位域），复用现成机制 |
| D8 | 实例名 mangle 成 `Vect_int`，与既有 `<Tag>_<name>` 方法名同族 | 跨 TU 稳定、可读；`-emit-header` 能直接写 `struct Vect_int` |
| D9 | V1 不做类型参数推导：自由函数显式 `vat@int(v, 0)`；**方法调用由接收者定类型**（`v->vat(0)`，唯一省略 `@` 的地方） | 推导只保留「接收者已经写死了 T」这一种形态，不需要重载/推导引擎 |
| D10 | 实例化点必须在模板**定义之后**；模板体内禁止自实例化 | 与 C「先声明后用」同构；自实例化是唯一的自噬递归源，直接关掉 |
| D11 | 不做特化、偏特化、SFINAE、模板模板参数、`typename`、依赖名、约束语法 | 鸭子协议 + 实例化点报错已经够用；每加一条都是诊断质量与单趟可行性的负担 |
| D12 | 每个 TU 实例上限 1024，实例化嵌套上限 64 | 单趟编译器不能靠磁盘缓存兜底，必须有硬闸 |
| D13 | token 重放保留原文件/行号 | 诊断指向模板源码，附 `requested from` 链 |
| D14 | `static` 局部在实例里**每实例一份** | 单态化的直接结果，写进规范避免 C++ 那样的 ODR 争论 |

---

## 1. 语言形式

### 1.0 为什么不是 `<…>`

|candidate|`Vect<int>`|`Vect[int]`|`Vect(int)`|`Vect of int`|`Vect@int`|
|---|---|---|---|---|---|
|需要成对闭合符（嵌套堆积）|是（`>>>`，C++03 还得写 `> >`）|是（`]]]`）|是（`)))`）|否（右结合无闭合）|**否**|
|与 C 表达式歧义|`Vec < int > x;` 是合法表达式链，需要 C++ 式声明/表达式消歧|下标/数组|调用/声明符|无|**无**（`@` 不是二元运算符）|
|像数组 / 像调用|—|像数组（用户第一反应）|像调用、像函数类型|不像|**不像**|
|多参数逗号归属|括号内|括号内|括号内|需要额外终结符|括号内|
|是否新记号|是（与 `<` 冲突）|复用下标|复用调用|软关键字 `of`|复用 `@`（已有前缀义）|

三条硬理由否掉 `<>`：

1. 词法：`>>` 的最大匹配让 `Vect<Vect<int>>` 需要改词法器或要求空格，`a<b>=c` 之类
   写法永远有例外；
2. 语法：`<` 在表达式里是合法运算符，`Vec < int > x;` 在 C 里本来能过，加泛型就要
   引入「能按声明解析就当声明」的回看规则——TCC 是单趟、无 AST，回看无处安放；
3. 人因：闭合尖括号是 C++ 可读性事故的固定来源（嵌套、错误信息、`typename` 消歧）。

`@` 是唯一同时满足「像英文 `at`」「不是 C 运算符」「已有用户直觉（`Vec@int` 读作
`Vec at int`）」的记号。

### 1.1 使用点

```
template-application   = template-name , "@" ,
                         ( type-argument
                         | "(" , argument-list , ")" ) ;
type-argument          = type-specifier | constant-expression ;
argument-list          = type-argument , { "," , type-argument } ;
```

```c
Vect@int            vi = {0};      /* 单参数：不写括号 */
Map@(char[:], int)  m = {0};       /* 多参数：必须括号 */
Buf@(int, 4)        four = {0};    /* 值参数：按位置对应，常量表达式 */
Vect@Vect@int       grid = {0};    /* 右结合：Vect@(Vect@int)，链上没有闭合符 */
Vect@int[:]         view = {0};    /* 后缀作用于整个类型：(Vect@int)[:] */
Vect@(int*)         ptrs = NULL;   /* 带 '*' 的实参：括号分组 */
@Vect@int           root = new Vect@int;   /* 托管引用指向 Vect@int（前缀 @ 吃整条后缀链） */
Vect@(@Node)        nodes = {0};   /* 元素是托管引用 */
```

规则：

* **R1（位置）** `@` 前面是**模板名** → 类型应用；`@` 出现在类型位置的起点（或
  `weak`/`own`/`shared` 之后）→ 托管引用。`@f"…"` 是第三种，只跟字符串。
  `@Vect@int` 因此无歧义：前缀要求后面是类型名/类型关键字，`Vect@int` 正好是。
* **R2（元数）** 模板声明了几个形参就吃几个实参；单实参可省括号，多实参必须
  括号（`Map@char[:], int` 报 C106）。
* **R3（后缀归属）** 实参只吃一个说明符；`*`、`[:]`、`[N]`、`!error`、参数表属于
  **外层**类型。要有后缀的实参就在括号里写：`Vect@(int*)`。
  于是最常用的 `Vect@int[:]` 读作「`Vect@int` 的切片」，不需要额外括号。
* **R4（结合）** 链右结合，`Vect@Map@char@int` 由各模板的元数唯一确定分组。

### 1.2 声明点

```
generic-parameter-list = "|" , [ generic-parameter , { "," , generic-parameter } ] , "|" ;
generic-parameter      = type-specifier , identifier          (* 值形参："int N" *)
                       | identifier ;                          (* 类型形参："T" *)

template-parameters    = "@" , ( generic-parameter
                               | "(" , generic-parameter , { "," , generic-parameter } , ")" ) ;
generic-struct         = ( "struct" | "union" ) , identifier , template-parameters ,
                         ( "{" , { member-declaration } , "}" | ";" ) ;
generic-declaration    = generic-parameter-list , declaration ;
```

```c
struct Vect@T        { T *data; int len; };          /* tag 定义：挂名（名字先于体） */
struct Map@(K, V)    { Vect@K keys; Vect@V vals; };
struct Buf@(T, int N) { T data[N]; int len; };       /* 值形参写成 C 参数表形状 */
union  Val@T         { T v; int i; };
struct Node@T;                                       /* 模板前向声明；不能实例化 */

|T| T  vat(Vect@T *self, int i);                     /* 函数模板：返回类型里有 T */
|T| T!error vget(Vect@T *self, int i);               /* 与 !error 后缀自然组合 */
|T| typedef Vect@T MyVec;                            /* 泛型别名 */
|T| typedef struct { T v; } Box;                     /* 匿名泛型 typedef */
```

### 1.3 形式规则：一条原则，零例外

**A3：形参表必须出现在该声明中第一次使用形参之前。**

|c 声明|形参出现在|形式|写法|
|---|---|---|---|
|`struct` / `union` 定义与前向声明|名字之后|挂名|`struct Vect@T { … }`|
|函数声明与定义|返回类型之后|前缀|`\|T\| T vat(Vect@T *self, int i)`|
|`typedef` 别名|目标类型之后|前缀|`\|T\| typedef Vect@T MyVec;`|
|`typedef` 匿名结构|体之后|前缀|`\|T\| typedef struct { T v; } Box;`|

每种声明**只有一种拼法**（不提供 `|T| struct Vect { … }` 这种等价写法），
所以不需要记忆偏好，只需要记 A3。挂名形式之所以只给 tag：tag 名是唯一
「出现在自己体之前」的名字。

### 1.4 `@` 的两个方向

|写法|含义|判据|
|---|---|---|
|`@Node h;`|托管引用指向 `Node`|`@` 在类型位置起点|
|`weak @Node p;`|弱引用|`weak`/`own`/`shared` 之后|
|`@Vect@int p;`|托管引用指向 `Vect@int`|前缀吃整条后缀链|
|`struct Vect@T`|模板定义（形参）|`@` 紧跟 tag 名|
|`Vect@int`|类型应用（实参）|`@` 紧跟类型名|
|`at@int(v, 0)`|函数模板的显式实例化|`@` 紧跟函数名|

### 1.5 括号规则（声明点 = 使用点）

* 1 个参数：不写括号（`Vect@int`、`struct Vect@T`）。
* ≥2 个参数：必须括号（`Map@(K, V)`、`Map@(char[:], int)`）。
* 单个参数但带后缀/前缀（`*`、`[:]`、`@`）：用括号分组（`Vect@(int*)`、
  `Vect@(@Node)`）——否则按 R3，后缀属于外层。

---

## 2. 语义

### 2.1 单态化与命名

模板不是类型，实例才是类型（C41 的裸 tag 名规则对实例同样成立）：

```c
Vect@int  a;                    /* 声明了 struct Vect_int 并完成布局 */
Vect@int *p = &a;               /* 同一 TU 里重复使用同一个实例 */
Vect@char b;                    /* 另一个完全不相干的类型：不能与 a 互赋（C109） */
```

mangle（跨 TU 稳定、`-emit-header` 可写、与既有 `<Tag>_<name>` 方法名同族）：

|写得|mangle|方法|
|---|---|---|
|`Vect@int`|`Vect_int`|`Vect_int_at`|
|`Vect@Vect@int`|`Vect_Vect_int`|`Vect_Vect_int_at`|
|`Map@(char[:], int)`|`Map_slice_char_int`|`Map_slice_char_int_at`|
|`Buf@(int, 4)`|`Buf_int_4`|`Buf_int_4_at`|
|`Vect@(int*)`|`Vect_int_p`|`Vect_int_p_at`|
|`Vect@(int[:])`|`Vect_slice_int`|—|
|`Vect@((int, int))`|`Vect_tuple_int_int`|—|
|`Vect@(@Node)`|`Vect_m_Node`|—|
|`Buf@(int, 4)` / `Buf@(int, -4)`|`Buf_int_4` / `Buf_int_m4`|`Buf_int_4_at`|

实参的 mangle 拼法：标量用拼写、指针加 `_p`、切片前缀 `slice_`、元组前缀
`tuple_`、托管引用前缀 `m_`（与普通指针区分，`@Node` 和 `Node *` 不会撞名）。

实例名属于**保留的 ABI 名**：用户手写同名 struct → `the instance name 'Vect_int' is already in use`（§8.1）。

### 2.2 模板本体是一个永不完整的类型

`struct Vect@T` 同时定义了一个**名义类型** `struct Vect`，它永远不完整，
只用于模板体内的自引用：

```c
struct Node@T {
    T *val;
    Node@T *next;       /* 模板自身：incomplete，只允许指针（C108） */
};
```

* 体内写 `Node@(T*)`、`Node@int` → C107（模板体内禁止实例化自己）。
* 体内 `sizeof(Node@T)` → incomplete（与 C 的 incomplete struct 一致）。
* 模板体内的 `T` 是**普通 typedef 名**（实现上就是这么绑的），所以
  `T *`、`T[:]`、`(T, int)`、`@T`、`T!error` 全部照常成立。
* 定义结束后 `T` 不可见（C105）；`struct Node@T;` 之后不能实例化（C108）。

### 2.3 值形参与常量

```c
struct Buf@(T, int N) { T data[N]; int len; };

Buf@(int, 4)   b4  = {0};   /* struct Buf_int_4 */
Buf@(char, 12) b12 = {0};   /* struct Buf_char_12 */
```

`N` 在体内按**普通常量符号**绑定（`VT_CONST`，值放在 `Sym.c`）：可进数组
长度、`T[:]` 长度、`case`、位域宽度，任何常量表达式；不是变量，不能取地址、
不能赋值（C110）。因为值就放在 `Sym.c` 这个 `int` 里，形参类型最宽是 `int`
（`long long N` 在声明处报错）。

值实参进 mangle：`Buf@(int, 4)` → `struct Buf_int_4`。负数的 `-` 不能出现在
标识符里，所以用 `m` 记符号：`Buf@(int, -4)` → `struct Buf_int_m4`。

值实参**必须带括号**，D5 的「单参数省略括号」只管类型实参：常量表达式不会
像类型那样自己停下来，`f@4(x)` 会被读成 `4(x)`——所以 `Arr@(4)`、`total@(4)(raw)`。

### 2.4 方法

方法仍然只是「第一个形参指向结构体的文件作用域函数」（§3.2 不变），
泛型只是把接收者换成模板形参。三种写法，实例化后是同一批函数：

```c
struct Vect@T {
    T *data; int len;
    T at(int i) { return data[i]; }   /* V3：体内写，接收者是隐式的 self */
};

|T| T vat(Vect@T *self, int i) { return self->data[i]; }  /* 前缀写法，一份覆盖所有实例 */
|T| T vsum(Vect@T *self);                                  /* 先声明后定义也行 */

void f(Vect@int *v) {
    int x = v->at(0);        /* Vect_int_at：体内方法与 |T| 方法同名同 ABI */
    int y = v->vat(0);       /* 接收者 = Vect@int → 实例化出 Vect_int_vat */
    int z = vat@int(v, 0);   /* 等价的显式写法 */
}
```

体内成员函数（§8.5）的规则：

* 方法体里的字段、方法**按名字直接用**：`data[i]` 就是 `self->data[i]`，
  `at(0)` 就是 `self->at(0)`。局部变量、参数、全局名照常按 C 的作用域规则先查，
  只有在这个方法实例的字段/方法表里命中才改写成 `self->…`。
* 方法可以调用同一个模板里**后面才写**的方法：实例化时先发全部原型，函数体
  统一留到翻译单元末尾（和 `|T|` 模板的函数体同一队列），所以互相调用、递归都行。
* `!error`、值形参 `N`、模板里再嵌实例照常可用：
  `T!error peek(int i) { return data[i]; }`、`int cap(void) { return N; }`。
* 方法体里可以第一次实例化**别的**模板（内层实例的 tag 和方法签名就在那里
  建出来，形参作用域各算各的）：`Leaf@int leaf = {k}; return (&leaf)->get();`。
  方法照旧 `p->m()` 调用；接收者是值时也可以写 `v.m()`（等价于 `m(&v)`）。
* 形参表按 C 的习惯写：`(void)` 和 `()` 都表示「只有接收者」，
  `void *p` 这种以 `void` 开头的形参不会被当成 `(void)`（收接收者 + 保住
  那个 `void`）。`const int a`、多形参、`(void *p, int n)` 都照常。
* 原型（只有名字没有体）在模板体里是错误：报「a member function needs a body」，
  因为这个体没有第二种写法可以补上（模板体只解析一次）。
* 非模板的结构体里写方法定义也是错误（`a function cannot be defined inside a
  struct`）：那是普通 C 结构体，方法按 §3.2 写在文件作用域。

* 调用点的 `@` 省略规则只针对**方法调用**：接收者的类型已经把 `T` 写死了，
  形状上再写一遍是噪音（D9）。
* 自由函数没有接收者，必须显式 `at@int(v, 0)`；这也是 `spawn`/`async`/函数指针
  拿泛型函数的唯一写法（`spawn vat@int(v, 0)`）。
* 模板本体 `struct Vect` 上不注册任何方法：`Vect *p = NULL; p->vat(0);` 报 C103。

### 2.5 与既有特性的交互

|特性|行为|
|---|---|
|切片 `T[:]`|`Vect@int[:]` 是 `Vect@int` 的切片视图；没有额外语法、没有新 ABI|
|切片嵌套|`int[:][:]` 已经覆盖「数组的数组」；这类需求不要用泛型（§7 风格规则）|
|元组|`\|T\| (T, int) find(Vect@T*, T)`；实例化后是普通元组类型|
|`!error`|`\|T\| T!error vget(Vect@T*, int i)`；失败路径复用 §8 的传播规则|
|接口 `interface`|每个实例独立实现自己的 vtable（`c6_auto_impl_ensure` 按实例 tag 建表）；**不需要** `I<T>` 形态|
|`foreach` 迭代器协议|`E next(T *self)` 里的 `E` 在实例化后是具体类型，C99「元素类型被擦除」自然不触发|
|`@T` 托管引用/GC|实例是独立 struct → 每实例一份描述符、一次环检；`struct Node@T { @Node@T next; }` 是静态环 → C66，和普通结构体一致。`@Vect@int` 上是 `new`/`->` 照常（`new Vect@int` 现在也认得出模板名），方法调用时接收者那份拷贝按借用处理（不计数、不读未初始化的临时槽，见 [gc-design.md](gc-design.md) §16 H4）|
|`spawn` / `async` / `task`|拿到的必须是具体函数：`spawn vat@int(v, 0)`；`\|T\|` 模板本身不能出现在 `spawn` 的操作数位置（C38 不变）|
|`defer` / `errdefer`|实例化体里照常展开，清理点数上限 C101 逐实例计算|
|`flexible array`|`struct Vect@T { int len; T data[]; };` 合法（尾元素类型是形参）|
|`-emit-header`|只输出实例：`struct Vect_int { int *data; int len; };` + `int Vect_int_at(struct Vect_int *, int);`|
|c99 后端|解析期已单态化 → 后端只看到普通 struct/函数，`tcc/c99-gen.c` 零改动|

---

## 3. 静态约束（C103–C116）

|约束|内容|诊断|
|---|---|---|
|C103|模板名不能单独作类型|`'Vect' is generic: write 'Vect@int'`|
|C104|实参个数必须与形参个数一致|`'Map' needs 2 arguments (got 1)`；`'Vect' takes no value argument`|
|C105|形参只在模板体/声明内可见；用到形参的位置必须晚于形参表（A3）|`'T' undeclared (a type parameter is only visible inside its template)`；`type parameter 'T' is used before it is declared: write '\|T\| T vat(...)'`|
|C106|多参数必须加括号|`a template with more than one parameter needs parentheses: 'Map@(char[:], int)'`|
|C107|模板体内禁止自实例化|`cannot instantiate 'Vect' inside its own definition: write 'Vect@T'`|
|C108|模板不完整（前向声明/未定义）时不能实例化，体内自引用只能取指针|`instantiation of undefined template 'Vect'`；`'Vect@T' is incomplete: only a pointer to it is allowed here`|
|C109|不同实例是不同类型，无隐式互转|`incompatible types for assignment: 'Vect_int' and 'Vect_char'`|
|C110|值实参必须带括号；值形参不可赋值/取地址；实参必须是常量表达式且能装进形参类型；形参宽度不超过 `int`|`constant expression expected`；`value argument 40000 does not fit in 'short'`；`a value parameter must be an integer type no wider than 'int'`；`a value parameter is written '<type> NAME'`|
|C111|每 TU 实例上限 1024|`too many generic instantiations in one translation unit (max 1024)`|
|C112|实例化嵌套上限 64|`generic instantiation nested too deeply (max 64)`|
|C113|`enum` 不能有类型参数|`enums cannot have type parameters`|
|C114|实例一律写裸名，不加 `struct`|`write 'Vect@int', not 'struct Vect@int'`|
|C115|形参不能与模板同名|`a type parameter cannot have the same name as its template`|
|C116|实例化要求模板定义在实例化点之前可见|`'Vect' is instantiated before its definition (forward declarations cannot be instantiated)`|

---

## 4. 边界用例

|写法|结果|
|---|---|
|`struct Vect@T { T *data; int len; };`|模板定义|
|`Vect@int v = {0};`|实例（布局在首次出现处完成；重复出现复用）|
|`Vect@Vect@int g = {0};`|嵌套实例，mangle `Vect_Vect_int`|
|`Map@(char[:], int) m = {0};`|多参数实例|
|`Buf@(int, 4) b = {0};`|值参数实例|
|`Vect@int[:] view = {0};`|`(Vect@int)[:]`，切片|
|`Vect@(int*) p = NULL;`|实参带 `*`，括号分组|
|`@Vect@int root = new Vect@int;`|托管引用指向实例|
|`Vect@int a = {0}; Vect@char b = {0}; a = b;`|C109，不同类型|
|`Vect v;`|C103|
|`struct Vect@int v;`|C114|
|`Map@char[:], int m;`|C106（逗号归属不明）|
|`struct Vect@T; Vect@int v;`|C108|
|`struct Vect@T { Vect@(T*) *p; };`|C107|
|`struct Vect@T { Vect@int q; };`|C107（体内自实例化，即便实参是具体类型）|
|`\|T\| T vat(Vect@T *self, int i);`|函数模板|
|`T vat@T(Vect@T *self, int i);`|C105（`@T` 出现在返回类型里的 `T` 之后）→ 改成 `\|T\|` 前缀|
|`\|T\| typedef struct { T v; } Box;`|匿名泛型 typedef（`Box_int`）|
|`\|T\| typedef Vect@T MyVec;`|泛型别名|
|`enum E@T { A, B };`|C113|
|`\|T\| void push(Vect@T *self, T x);`|函数模板（形参只出现在参数表里）|
|`spawn vat(v, 0);`|C38 的既有诊断 + 提示 `spawn vat@int(v, 0)`|
|`sizeof(Vect@T)` 在体内|C108（incomplete）|
|`Vect@int` 在 `-emit-header` 输出里|`struct Vect_int { … };`，泛型信息不跨 TU|

---

## 5. 实现

### 5.1 挂载点

|语法|位置|
|---|---|
|模板定义 `struct X@params`|`struct_decl()`（`tcc/tccgen.c:6541`）：读完 tag（`v = tok, next();`，约 6561）后判 `tok == '@'`，解析形参 → `local_scope++` + 形参入符号栈 → 体照常走 `c6_is_def` 分支 → `local_scope--` 并弹栈|
|模板前向声明|同上，`tok` 为 `';'` 时不解析体|
|使用点 `X@args`|`parse_btype()` 的 `default`（标识符）分支：`sym_find()` 未命中、`struct_find()` 未命中之后（§11 记录的 tag 回退旁边）加第三查 `c6_generic_find()`；命中要求 `tok == '@'`。**必须就地消费**：`@` 的前缀托管分支在 `parse_btype()` 的 while 顶部（`tcc/tccgen.c:7104`），晚一步就会被当成托管引用|
|函数/typedef 模板 `\|T\|`|文件作用域 `decl()` 入口判 `tok == '\|'`（`\|` 不能起一个声明或表达式语句；`foreach \|…\|` 的分派在 `block()`，不冲突）；与 struct 共用「压形参作用域」的同一段代码|
|形参绑定|类型形参 = `SYM_TYPEDEF`（`struct_decl` 前压栈）；值形参 = 普通常量 `Sym`（`VT_CONST` + `Sym.c`），因此 `N` 自动可进 `expr_const`|
|实例化体|`skip_or_save_block(&body)` 捕获 token（`tcc/tccgen.c:1022` 声明、`c6_lit_parse()` 8519/8545 与 `c6_tuple_speculate()` 是先例），实例化时重放|
|实例表|`C6Generic`（模板：名字/形参种类/tag/体 token）+ `C6Instance`（mangle 名 → tag），`c6_tu_reset` 复位|
|方法注册|实例 tag 走既有 `c6_method_register()` / `c6_method_lookup()`；泛型方法在实例化后按实例 tag 注册|
|体内成员函数|`c6_gen_scan_methods()` 在捕获模板体时切出「`NAME(...) {…}`」成员（`C6GenMeth`：名字 + token）；`struct_decl()` 的成员循环认出函数定义就跳过体（`c6_meth_end`），不再当字段解析|
|体内方法的原型/体|实例化时 `c6_gen_meth_decl()` 把成员拆成「原型 + 体」：原型插入 `struct <实例> *self` 作首参、在 bind 期间重放（签名里还能写 `T`）；体进 `c6_gen_methq` 队列，`c6_gen_flush()` 在单元末尾重放 `gen_function()`|
|体内方法的隐式 self|`unary()` 的标识符分支：字段命中 → `self->field`（`c6_gen_self_member()`），方法命中且后面是 `(` → `self->m(...)`（`c6_gen_self_method()`）；名字先按普通作用域查，落空才走这里|
|实例符号的寿命|实例 tag 和它的方法符号在 `sym_pop()` 里是「粘性」的（`c6_sticky_add()`）：名字留在 token 表、内存不回收，被 `keep == 0` 的弹栈带走时重新挂回全局栈——模板在形参表/块里第一次应用也不会丢|
|`-emit-header`|`c6_hdr_extra()`（`tcc/tccgen.c:10880`）同一套路，名字用 mangle 而不是序号|
|诊断链|实例化栈记录 `requested at file:line`，`tcc_error` 前置 `in 'Vect@int'`|

### 5.2 实例化流程

1. `parse_btype()` 见到模板名 + `@` → 解析实参（按形参种类：类型实参走 `parse_btype`
   的抽象声明符路径，值实参走 `expr_const`）。
2. mangle → 查实例表；命中直接复用。
3. 未命中：在实例表登记（先解决递归自引用：把 tag 先建成 incomplete，
   这样 `Vect@Vect@int` 的内层实例在布局前就有符号）。
4. `local_scope++`，把形参按 `T → CType` / `N → 常量` 压符号栈。
5. 若模板本体已定义：重放捕获的体 token，走正常的成员/函数解析 → 布局 →
   注册方法；体内成员函数在这里只发原型，体进队列等单元末尾。
6. `local_scope--`，弹栈；返回实例 `CType`。
7. 全程在实例化栈上记一条，退出时弹出；栈深 > 64 报 C112，实例数 > 1024 报 C111。

### 5.3 关键不变量

* **不新增类型系统**：实例是真 struct，形参是普通 typedef/常量符号 → 现有
  布局、方法、接口、切片、元组、GC 描述符、后端全部按原路径工作。
* **不新增词法**：`@`、`|` 已经是简单记号；`|` 只在 `tccpp.c` 的 `parse_simple`
  里出现（`@` 的 ARM 汇编注释特例只在 `PARSE_FLAG_ASM_FILE` 下生效，不影响 C 源）。
* **零回看**：所有判定都是「`tok` + 至多一个 token」（唯一的多 token 扫描是捕获
  模板体，用的是既有的平衡括号扫描 `skip_or_save_block`）。
* **不改后端**：没有任何泛型相关代码进入 `*-gen.c` / `c99-gen.c`。

### 5.4 风险清单

|风险|处置|
|---|---|
|重放期间的文件/行号归属|token 自带原来的 file/line（`c6_tuple_speculate` 已经踩过这个坑），诊断因此指向模板源码|
|体里含 `#include`/宏|宏在捕获前已展开；模板体不允许跨 `#include` 边界（诊断要求把模板定义写在一个文件里）|
|`static` 局部|每实例一份（D14），`-emit-header` 不导出|
|实例名与用户符号撞车|保留名 + 冲突诊断；`-emit-header` 里以实例名出现，链接期可发现|
|`-run` 模式|无影响：单态化发生在解析期，`tcc_run` 只看到普通函数|
|实例爆炸|C111/C112 双闸；`-Wgeneric-depth` 只是风格告警（见 §7）|
|模板在形参表/块里第一次应用|实例 tag 和方法符号标记为粘性，`sym_pop()` 不摘名字、不回收，`keep == 0` 时挂回全局栈；体内方法的原型还会在下一个 `decl()` 处补写一次（`c6_gen_sig_flush()`）|
|体内方法体与函数体的顺序|一个函数不能在另一个函数里生成：体统一进队列，`c6_gen_flush()` 在单元末尾写；先发全部原型所以互相调用、递归都不受影响|
|托管实例上的方法调用|接收者 `@Vect@int` 拷进调用临时槽时是借用（`C6_GCM_INIT \| C6_GCM_FRESH`），否则会读未初始化的栈去释放旧值；`tests/generic.c6` 与 `tests/gc.c6` 各有断言|
|一个进程里编多个 TU|每个 TU 结尾必须清泛型表（`tccgen_compile()` 的出口也调 `c6_tu_reset()`）：模板体、方法体存的是预处理器的 token 串，下一个 TU 会换掉那块 token 池，等到下一个 TU 开头再释放就是悬空指针——「TU1 有模板 + TU2 随便什么」直接段错误（既有缺陷，`tests/tu/generic.c6` 现在盯着）|
|方法体里第一次实例化另一个模板|内层实例的 tag、方法签名都在那时才建，而外层方法体的 `self`/参数正躺在同一个作用域号上 → `redeclaration of 'self'`。已修：`c6_gen_instance()` 声明成员签名时把 `local_scope` 抬一层（`save_scope + 1`），那份原型有自己的形参作用域；`tests/generic.c6`「体内方法里第一次实例化另一个模板」盯着。方法是 `->` 调用（`(&leaf)->get()`），和方法语法的既有规则一致|
|体内方法体被当成函数字面量声明|`block()` 的 `c6_lit_decl_lead()` 探针会吃掉 `NAME = …`：命中本实例的字段/方法名时不探（`c6_gen_self_name()`），否则 `len = 3;` 会被探针重放坏掉|

---

## 6. 和 C++ 模板的差别（为什么不会长成第二个 C++）

* 没有特化/偏特化 → 没有「选了哪个模板」的推理；
* 没有 SFINAE、没有重载解析 → 实参类型不匹配就是一条诊断，不会「换个重载试试」；
* 没有推导（唯一例外是方法接收者）→ 每个实例名都能在源码里指出来；
* 没有 `::` → 没有依赖名，因此不需要 `typename`/`template` 消歧关键字；
* 没有 `>>` 词法问题 → `Vect@Vect@int` 直接写；
* 实例必须在定义之后 → 与 C「先声明后用」一致，不会出现「TU 末尾才编译、报错找不到出处」；
* 诊断最多两层：`in 'Vect@int' (requested at f.c:12)`。

---

## 7. 风格规则（配合 `-Wgeneric-depth`）

1. 显式类型实参 ≥2 层时起别名，正文只写别名：

   ```c
   typedef Vect@int     IntVec;
   typedef Vect@IntVec  Grid;          /* 不要到处写 Vect@Vect@int */
   typedef Map@(char[:], IntVec) NameMap;
   ```

2. 「数组的数组」用切片，不进泛型：`int[:][:]` 比 `Vect@(Vect@int)` 便宜且免分配。
3. 形状固定的缓冲用值形参：`Buf@(char, 256)` 比 `Vect@(Vect@char)` 清楚。
4. 别名用语义名（`IntVec`、`NameMap`），不要用机械拼接的名字（`VecInt`），
   后者和 mangle 名长得一样，读代码时容易当成 ABI 名。

---

## 8. 未决问题

1. **实例名保留策略**：是「用户不得定义 `Vect_int`」还是「冲突时加哈希尾缀」。
   倾向前者（可读、`-emit-header` 稳定），实现时在 `decl()` 里查实例表即可。
   当前实现**没有做这个检查**：手写 `struct Vect_int` 会和 `Vect@int` 的实例撞名
   （谁先谁赢），见 §9 阶段 2 的第 11 条。
2. **`-Wgeneric-depth` 的阈值与是否进 `make test`**：已按建议实现——默认关闭，
   `-Wall` 打开，**写出**的应用链深于 2 层时告警。测试不需要额外开关：
   `tests/run-tests.sh` 的诊断组支持文件内的 `// FLAGS …` 行，
   `tests/errors/generic.c6` 用 `// FLAGS -Wgeneric-depth` 加一个
   `test_chain_depth` 片段（告警 + 一个真错误，两样都要出现在 stderr）。
3. **模板实例的 `drop` 方法**：`|T| void drop(Vect@T *self)` 每实例一份，GC 描述符
   按实例指针填；与 `gc-design.md` 的环检测配合需要在实例化后重跑一次环检。
4. **`module` 前缀**：模板名与实例 mangle 名都要带模块前缀（`std__container__vec__Vect_int`），
   冒号点号形式的解析复用 `c6_pkg_name` / `c6_mangle_tok`。
5. **体内成员函数（V3）**：允许在模板体内直接写方法定义：

   ```c
   struct Vect@T {
       T *data; int len;
       T at(int i) { return data[i]; }   /* V3：体内成员，`|T|` 前缀随之消失 */
   };
   ```

   它是 `|T|` 前缀形式的严格超集，落地后泛型函数的声明点就收敛成一种，
   且 ABI 与现在的 `<Tag>_<name>` 完全一致。**已落地**（§9 阶段 3 第 11 项）：
   字段/方法名隐式指向 `self`、先写的方法能调后写的方法（原型先发、体后写）、
   `!error` 与值形参照常可用；`|T|` 写法继续保留，两者 ABI 相同。
6. **V2 闭包与 `|…|`**：[closure-design.md](closure-design.md) D4 明确不引入捕获
   列表（捕获是隐式的），所以 `|…|` 只属于类型/值绑定，没有第二种用途。若将来要
   加捕获列表，必须换记号（例如 `(…)` 或 `capture(…)`），这条要写进 closure 文档。

---

## 9. 实施顺序与验收

**阶段 1（V1 核心，可独立落地）**

|#|内容|状态|
|---|---|---|
|1|`C6Gen` / `C6GenInst` 表 + `c6_tu_reset` 复位|✅|
|2|`struct_decl()` 的 `@` 形参解析（类型形参、形参符号栈）|✅|
|3|`parse_btype()` 的模板名第三查（`sym_find` → `struct_find` → 模板）+ `@` 实参解析 + mangle|✅|
|4|`skip_or_save_block` 捕获 + 重放实例化（`typedef` 绑形参 + `c6_decl_tokens`）|✅|
|5|函数/typedef 模板的 `\|…\|` 前缀（方法要用它）|✅ `\|T\| T vat(Vect@T *, int)`、`\|K, V\|`、`\|T\| typedef …`；先声明后定义、`T!error`、`spawn`/`thread` 都可用|
|6|约束 C103–C116|✅ C103–C116 都有诊断 + 用例（C106 与 C104 同一条消息：多参数必须加括号）；C109 由普通类型检查给出（`cannot convert 'struct Vect_char' to 'struct Vect_int'`）|
|7|方法：实例 tag 注册 + 接收者定类型调用|✅ 两种都行：接收者写具体实例（`int vect_sum(Vect@int *v)`），或 `\|T\|` 一份覆盖所有实例（`p->vat(0)` 按 `p` 的实例定 T）|

**阶段 2（补齐）**

|#|内容|状态|
|---|---|---|
|8|值形参（常量绑定）+ C110|✅ `struct Buf@(T, int N)` / `Buf@(int, 4)`，mangle 带实参拼写，`\|T, int N\|` 方法模板可覆盖所有实例（`tests/generic.c6`、`tests/abi/generic.c6`）|
|9|模板前向声明 + C108|✅ `struct Fwd@T;` 可用；实例化报 `instantiation of undefined template`|
|10|实例上限 C111/C112；`-emit-header` 输出实例|✅ C111/C112（1024 / 64）；`-emit-header` 用实例名写出 `struct pkg__Vect_int` 与实例函数原型|
|11|`module` 前缀（模板名、实例 mangle 名，§8.4）与实例名冲突检查（§8.1）|✅ 实例名与普通名字一样带模块前缀（`m2__Vect_int`、`m2__id_int`、`m2__MyVec_char`），所以符号与 `-emit-header` 输出一致；用户手写同名 tag → `the instance name 'Vect_int' is already in use`|

**阶段 3（收敛）**

|#|内容|状态|
|---|---|---|
|11|体内成员函数（§8.5）|✅ `struct Vect@T { T *data; int len; T at(int i) { return data[i]; } }`：字段/方法名隐式走 `self`、方法可先用后定义、值形参 `N`、模板里嵌实例、`union` 模板、`!error` 方法都可用；符号仍是 `<实例>_<方法>`（`Vect_int_at`，`tests/abi/generic.c6` 用纯 C 直接调），`-emit-header` 照旧输出原型。`\|T\|` 写法**继续保留**（`\|T, int N\|` 一份覆盖所有值形参实例，体内写法做不到），不设废弃告警|
|12|`-Wgeneric-depth`|✅ 默认关、`-Wall` 开，**源码里写出来的**应用链深于 2 层时提示起别名：`Vect@Vect@Vect@int` 报一次、`Vect@Vect@int` 不报，每个实例只报一次；模板体里「`A` 里嵌 `B`、`B` 里嵌 `Vect`」是正常嵌套，按写的 1 层算。深度直接读实例的实参（实参是实例就带上它自己的深度），所以重放不会虚报|

**测试**（已落地，都在 `tests/run-tests.sh` 的既有分组里）

|文件|内容|
|---|---|
|`tests/generic.c6`|模板/实例/嵌套/自引用/别名/方法/切片/元组/值形参/函数体内定义模板/体内成员函数（隐式 `self`、先用后定义、mangled 符号互调、模板里嵌实例、方法体里第一次实例化别的模板、托管实例上的方法不掉计数、形参表 `(void)`/`()`/`void *p`），26 个 `test` 块|
|`tests/tu/generic.c6`|`tests/generic.c6` 的第二个翻译单元：两个 TU 各自声明并实例化同一批模板，盯「泛型表按 TU 复位」（§5.4 那条多 TU 缺陷）|
|`tests/generic_fn.c6`|`\|T\|` 函数/typedef 模板：一份方法覆盖所有实例、显式应用与接收者写法等价、多形参、先声明后定义、切片形参、`T!error` + `catch`、`spawn`/`thread`，9 个 `test` 块（起线程那块标了 `// BARE-SKIP`：裸机没有 pthread，`examples/boot/runtests.sh` 跑剩下 8 块）|
|`tests/errors/generic.c6`|C103–C116 的负例：裸模板名、实参个数、实参类型、重复/撞名形参、值形参、`\|…\|` 用法、实例名占用、实例互赋、非模板结构体里写方法、模板体里只写方法原型，加 `-Wgeneric-depth` 的深度告警，24 个片段|
|`tests/abi/generic.c6` + `generic_plain.c`|跨 TU：`Vect@int` = `struct Vect_int`，方法符号 `Vect_int_vect_sum`，体内方法同一符号（`Vect_int_isum` 由纯 C 侧直接调），值形参实例 = `struct Buf_int_4`|
|`tests/c99/generic.c6`、`tests/c99/generic_fn.c6`|c99 后端输出可编译，行为一致（含体内方法：输出里就是普通 `Cnt_int_last()`）|

原计划单独拆文件的 `tests/generic/nest.c`、`suffix.c` 没有单独立文件：这些
形态都并进 `tests/generic.c6` 与 `tests/generic_fn.c6` 了，阶段 1–3 的 12 项
全部 ✅，没有遗留条目。

跨 TU 按值传实例要注意 c99 后端：它把 8 字节的 struct 打包成一个整型寄存器
传（和 SysV 一致），更大的 struct 用隐藏返回指针（和系统 ABI 不符）——这是与
泛型无关的既有后端限制，所以 `tests/abi/generic.c6` 里按值过边界的只有 8 字节
的 `Pair@(int, int)`，`Vect@int` 一律走指针。

验收：`make test`、`make c99test`、`make selfhost`、`make c99selfhost` 全绿。
