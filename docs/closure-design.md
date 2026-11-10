# c6 函数字面量与闭包设计

状态：**V1 已实现**（无捕获的字面量 = 裸函数指针）。
**V2（捕获式闭包）已实现**：本文是 V2 的规格（与 `tcc/tccgen.c`、`c99-gen.c`
同步）；实施清单与用例对照见 [closure-todo.md](closure-todo.md)，语法与约束
编号见 [grammar.md §3.10](grammar.md)。

相关：[grammar.md §3.10](grammar.md)（V1 语法与 C80–C83）、
[gc-design.md](gc-design.md)（`@T` 的计数、描述符、环检测与跨线程规则）、
`tests/fn_lit.c6` / `tests/errors/fn_lit.c6` / `tests/abi/fn_lit.c6`。

---

## 0. 决策速览

| 编号 | 决策 | 理由（详见 §12） |
|---|---|---|
| D1 | 字面量仍是**表达式**，值仍是「函数」；无捕获时类型与 ABI 与 V1 完全一致 | 不破坏 V1、C ABI 零变化 |
| D2 | 捕获式闭包的类型是**托管引用** `closure RET (参数)`，值是一个机器字 | 按值胖结构体含托管字段，C68 禁止拷贝，闭包将无法赋值/传参 |
| D3 | 闭包对象是隐藏的 `@struct __c6_closure_<sig>`：`{ code, env }`；环境是另一个隐藏的 `@struct __c6_env_<n>` | 签名级规范类型可共享；env 的具体字段只属于某个字面量，分开才能两全 |
| D4 | 捕获是**隐式、按值、在字面量求值时快照**；不引入捕获列表 | V1 已把 `[` 给了下标/切片；隐式捕获对用户最省事 |
| D5 | 体里的捕获名是 **env 里的一份副本**，可读可写；写只改副本，不碰外层变量 | 与「按值捕获」自洽；`int() { n++; }` 是闭包自己的状态 |
| D6 | `closure` 是**软关键字**，只在类型位置生效；`int closure = 1;` 照常 | 与 `co`/`spawn`/`task`/`module` 一致 |
| D7 | 捕获集合由**扫描 pass** 在字面量处精确求出：用编译器自己的解析器把体解析一遍（不生成代码），按符号序号区分体外的名字 | 在 TU 末尾分析太晚（env 布局在字面量处就要定）；纯 token 扫描分不清遮蔽 |
| D8 | 调用点把 env 作为**隐藏的第一实参**（sret 缓冲之后），复用方法/接口的 `c6_self_pending` 机制 | 五个后端已经用同一条路验证过 self 参数 |
| D9 | 函数指针 → 闭包是**隐式转换**，用「按签名一份」的跳板 + 一个只装函数指针的 env | 无捕获字面量仍是裸指针；需要放进闭包时统一包装，变参除外 |
| D10 | 闭包 → 函数指针**禁止**；闭包不透明（不能取字段、不能 `new`、不能当普通指针用） | 无 env 的函数指针装不下闭包 |
| D11 | V2.0 的闭包**不跨线程/任务**；`shared`/`weak`/`own` 修饰闭包一律报错 | env 的原子性和 `shared` 捕获需要额外的上下文传播，先不做 |
| D12 | 捕获的托管引用按普通 `@T` 字段 +1 进 env，随 env 释放；闭包类型的静态环检测用「签名 → 被捕获类型」的保守边 | 复用 GC 的描述符/释放/环检测，不新增运行期概念 |

---

## 1. 语言形式

### 1.1 函数字面量（V1，已实现）

```
function-literal  = type-specifier , [ "!error" ] ,
                    "(" , [ parameters ] , ")" , compound-statement ;

void (*f)(int) = void(int x) { printf("%d\n", x); };   (* 显式声明 *)
void(int x) { printf("%d\n", x); }(1);                 (* 就地调用 *)
f = void(int x) { printf("%d\n", x); };                (* 推断声明 *)
```

V1 的三条性质（V2 全部保留）：

1. **字面量是表达式，值是函数**，不是「可调用对象」。`f(1)`、`apply(f, 1)`、
   `f` 存进结构体/数组、传给 C，全部按已有的函数指针规则走；
2. **没有新的值类别**。无捕获时值就是 `T (*)(P…)`，和写一个函数名完全等价
   （C ABI 零变化，`-emit-header`、`module` 名字改写都不用特殊处理）；
3. **体是稍后编译的**，和 `static inline` 一样在翻译单元末尾
   （`gen_function()`）发射，内部名 `__c6_fn_<序号>`，`static`。

### 1.2 V2 新增：`closure` 类型

```
closure-type = "closure" , type-specifier , [ "!error" ] ,
               "(" , [ parameter-type-list ] , ")" ;
```

`closure` 是**软关键字**（和 `co`/`spawn`/`task` 一样）：只有出现在类型位置、
且没有被同名变量/typedef 遮蔽时才是关键字。实现上在 `parse_btype` 里按名字
识别（和 `new` 一样），**不**把它加进 `c6_soft_kw()` 的语句分派——`block()`
的 `switch` 默认分支是 `c6_task_decl()`，加进去会把语句开头的 `closure …`
误当成任务声明。

```c
closure int(int) f;                         (* 变量 *)
closure int!error(int) g;                   (* 可失败的闭包 *)
closure void(int[:], int) each;             (* 切片、多参数 *)

typedef closure int(int) IntOp;             (* 类型别名 *)

closure int(int) apply(closure int(int) g, int x)
{
    return g(x);                            (* 调用点自动带 env *)
}

struct Holder { closure int(int) cb; };     (* 字段；Holder 因此是托管结构体 *)
closure int(int) table[4];                  (* 闭包数组：4 个托管引用 *)
```

* `closure RET (P…)` 是一个**托管引用类型**，与 `@T` 同族：
  `sizeof == sizeof(void *)`，可拷贝（+1）、可赋值、可返回、可作实参、可存进
  `@T` 槽/结构体字段/数组；`f == NULL`、`if (!f)` 照常；
* 它**不是**函数指针：`closure int(int)` 与 `int (*)(int)` 是两种类型。
  前者能带环境，后者不能；相互转换的规则见 §3.5；
* `closure` 不能被 `@` 再修饰（`@closure …` 报错）；也不能加
  `shared`/`weak`/`own`（V2.0，见 §7.3）。`const`/`volatile` 可以修饰
  闭包变量本身，和普通 `@T` 一样；
* 具体表示、命名与 ABI 见 §3。

### 1.3 字面量的两种值

一个字面量在**求值点**根据「体里是否真的引用了外层的值」分成两种：

| 捕获集合 | 值的类型 | 运行期 | 例子 |
|---|---|---|---|
| 空 | `RET (*)(P…)`，与 V1 完全相同 | 零分配、零额外数据 | `int(int x) { return x + offset; }`（`offset` 是文件作用域） |
| 非空 | `closure RET (P…)` | 分配 env + 闭包对象，捕获值拷贝进 env | `int(int x) { return x + n; }`（`n` 是外层局部） |

「捕获集合」是**编译期**概念；一个体里只引用文件作用域名字的字面量永远是
裸函数指针，不会因为「看起来像闭包」而多分配。只有 `C6_CAP_TYPE` /
`C6_CAP_ZERO` 条目（类型名、只在不求值上下文里出现的值）而没有
`C6_CAP_VALUE` 时也一样：这些别名在编译期压进符号表就够了，不需要运行期
env，`lit->capturing` 只由 `C6_CAP_VALUE` 置位。

推断声明也跟着值类型走：`f = int() { … n … };` 把 `f` 推断成
`closure int()`（V1 推断成 `int (*)(void)`）；没有按值捕获时仍是 V1 的函数
指针。已有声明的赋值按 §3.5 的转换规则检查。

### 1.4 例子

```c
static int offset = 100;

closure int(int) make_adder(int n)
{
    /* n 在字面量求值时被拷贝进 env；返回后 n 的栈槽消失，env 仍然活着 */
    return int(int x) { return x + n; };
}

int main(void)
{
    closure int(int) add5 = make_adder(5);
    if (add5(1) != 6) err(1, "closure");
    add5 = NULL;                              /* 释放 env 与闭包对象 */
    return 0;
}
```

```c
int main(void)
{
    int n = 1;
    closure int() counter = int() { return ++n; };  /* 副本 n = 1 */
    n = 100;                                         /* 不改闭包里的副本 */
    counter();                                       /* 2 */
    counter();                                       /* 3：状态留在 env 里 */
    return n != 100;
}
```

```c
/* 无捕获：仍是裸函数指针，可以直接交给 C */
qsort(v, n, sizeof v[0], int(const void *a, const void *b) { … });
```

---

## 2. V1 回顾（已实现）

```c
static int offset = 100;                  /* 文件作用域：体里看得见 */
int (*f)(int) = int(int n) { return n + offset; };   /* 可以 */
```

体里只看得见**自己的参数**和**文件作用域**的名字。写在外层的局部变量、形参、
块作用域 `typedef`/tag 都看不见：

```c
int x = 1;
int (*f)(void) = int() { return x; };     /* V1：错误 C80；V2：C93 */
```

V1 选择报错而不是「就按引用用」：字面量的体可能在函数返回之后才被调用，
局部变量在返回时就已经消失；按引用捕获等于给用户一个必然悬空的读写窗口。
按值捕获需要一台 env，V2 补上。

实现上，V1 的捕获检测是「否定表」：`C6Lit.caps` 在解析字面量头时收集当前块
作用域里的所有名字（`local_stack` 中 `sym_scope != 0` 的标识符），体编译期间
任何一次符号解析（值、`typedef`、tag 三条路径）命中表内名字就报 C80 —— 不需要
做自由变量分析，也不会漏。V2 把这张表换成**精确的捕获集合**（§5）。

---

## 3. 值形状与类型

### 3.1 三个东西

```
(* 用户写的类型 *)        (* 运行期对象 *)
closure int(int)          struct __c6_closure_<hash> {        (* 托管对象，@ *)
                              int (*__c6_code)(void *__c6_env, int);
                              @__c6_env_base *__c6_env;       (* 强引用 *)
                          };

                          struct __c6_env_<n> {               (* 托管对象，@ *)
                              int __c6_c0;                    (* 捕获 1：n *)
                              @Node __c6_c1;                  (* 捕获 2：+1 *)
                              …                               (* 布局在字面量处定死 *)
                          };
```

* **用户可见的值**只有第一层：一个 `@` 指针。它的静态类型是
  `closure RET (P…)`，`sizeof` 是一个机器字；
* **闭包对象**里放 `code` 与 `env` 两个字段。`code` 是普通（非托管）函数指针，
  `env` 是一个被擦除的托管引用，指向该字面量的环境；
* **环境对象**的字段就是捕获值。字段名是编译器生成的 `__c6_c<N>`，用户不可见。

为什么不是「按值胖指针」：一个含 `@` 字段的结构体在 c6 里不能按值拷贝
（C68）。若 `closure` 是这样的结构体，`f = g;`、把闭包传给函数、存进字段
全都非法，闭包基本不可用。做成 `@` 托管引用后，拷贝/赋值/返回直接复用
`@T` 的全部规则，一行新代码都不用写。

### 3.2 布局（固定，不随 `#pragma pack` 变）

```
struct __c6_closure_<hash>:
    offset 0          RET (*__c6_code)(void *__c6_env, P…)
    offset PTR_SIZE   @__c6_env_base *__c6_env
    size              2 * PTR_SIZE
    align             PTR_SIZE

struct __c6_env_<n>:
    字段按 §5.6 的顺序自然对齐；偏移在字面量处算好，存进 C6Lit；
    环境只被本 TU 生成的代码访问，所以不需要跨 TU ABI，但仍固定布局以便
    描述符与访问代码一致。
```

闭包对象的布局必须跨 TU 一致（`-emit-header`、`import`、两 TU 互传闭包），
所以**像切片一样手工定布局**，不走 `struct_layout()`（后者会被 `#pragma pack`
改写）。环境对象只在创建它的 TU 内被访问（`code` 与 env 的字段访问都由同一个
前端生成），可以直接用 `struct_layout()`：即使受 `#pragma pack` 影响也没关系，
偏移在字面量处一次算好，访问代码用的就是同一份偏移。

两个合成 tag 都挂在 `global_stack` 上。注意 `struct_decl()` 末尾的
`c6_gc_collect()` 有一条「含托管字段的结构体不能定义在块里」的检查
（`local_scope != 0` 就报错）；合成类型是在函数体里按需创建的，必须放行：
用一个 `c6_gc_synthetic` 标志（或临时把 `local_scope` 存成 0 再恢复）绕过
这条检查，否则 `closure` / env 在第一个字面量处就编译不过。

`__c6_env_base` 是一个空的内部结构体（`struct { char __c6_unused; }`），只用来
给 `@` 一个被指类型；真正的字段布局在 `__c6_env_<n>` 里。运行期释放只认对象头
里的描述符，不认静态被指类型，所以擦除是安全的。

### 3.3 规范签名与命名

`closure RET (P…)` 的类型身份由**签名的规范化文本**决定：

* 返回类型 + 每个参数类型（先经 `convert_parameter_type()` 归一：数组/VLA→
  指针、函数→函数指针；去掉参数顶层 `const`/`volatile`，与 C 的函数类型
  兼容规则一致）+ `!error` 位 + 变参位；托管限定符（`@`/`shared`/`weak`/`own`）
  也进文本，`@T` 和 `T *` 是不同签名；函数调用约定（如果非默认）也进文本，
  否则两个 ABI 不同的函数指针会拿到同一个闭包类型；
* 规范化文本里 struct/union/enum tag 用声明时的名字（`module` 前缀已包含在
  名字里），切片/元组/嵌套闭包按结构递归展开；
* 对规范化文本做 **FNV-1a 64 位**哈希，类型名是
  `__c6_closure_<16 位十六进制>`。同一 TU 内相同签名只建一次（`struct_find`
  命中就复用）；不同 TU 各自算，但名字相同、布局相同，因此 ABI 兼容；
* 哈希碰撞（不同签名同名）直接报内部错误，不做后缀：16 位十六进制下不可能
  发生，静默加后缀反而会破坏跨 TU 名字一致性；
* 签名里**不能出现匿名 struct/union/enum，也不能出现块作用域 tag**（C90）：
  匿名类型每个声明都是新类型，哈希不稳定；块作用域 tag 在 TU 末尾已经不可见；
* 跨 TU 使用同一个闭包签名时，签名里的 struct/union 定义必须一致——和普通 C
  函数原型一样，ABI 靠两边声明一致，编译器不额外校验布局。

`__c6_env_<n>` 的 `<n>` 是本 TU 的字面量计数器，跨 TU 不要求一致（env 不出现
在任何公开签名里）。

### 3.4 类型规则

| 写法 | 含义 |
|---|---|
| `closure int(int)` | 托管引用，`sizeof == sizeof(void *)` |
| `closure int(int) *p` | 指向「闭包引用」的普通指针（和 `@T *` 一样） |
| `closure int(int) a[4]` | 4 个闭包引用的数组（托管字段数组） |
| `closure int(int) s;` 在结构体里 | 托管字段；含它的结构体适用 C68（不能按值拷贝） |
| `closure int(int)` 作返回值/形参 | 一个机器字的托管引用；`return` 转移一份 |
| `const closure int(int) f` | 常量闭包变量（不能 reassign，对象本身仍可调用） |

* `closure` 不能修饰 `void` 以外的返回类型？不 —— 返回类型随便写，和字面量
  一致；`closure void(int)` 是返回 `void` 的闭包；
* 不能 `new closure …`：闭包只能由字面量或函数指针包装产生（C97）；
* 不能 `f->__c6_code`、不能 `*f`、不能 `f + 1`（C97）；
* `closure` 可以出现在 typedef、`typeof`、`sizeof`、强转、`-emit-header` 里；
* 两个不同签名的 `closure` 不兼容；同一签名就是同一个类型。

### 3.5 转换

| 从 \ 到 | `RET (*)(P…)` | `closure RET (P…)` |
|---|---|---|
| 非捕获字面量 | 直接（V1） | **包装**（隐式；闭包对象 + 存放目标函数的小 env，两次分配；变参见下） |
| 函数指针变量/表达式 | 直接 | **包装**（隐式；同上两次分配；变参报 C92） |
| 按值捕获的字面量 | **错误 C93** | 直接（值本身就是闭包） |
| `closure` 值（同签名） | **错误 C91** | 直接（`@T` 拷贝，+1） |
| `closure` 值（不同签名） | **错误 C91** | **错误 C91**（要换签名就写成字面量/包装） |
| `NULL` | 可以 | 可以（空闭包） |

包装发生在这些消费点：声明/初始化、赋值（`vstore`，含聚合初始化
`decl_initializer` 走 `vstore` 的路径）、函数实参（`gfunc_param_typed`）、
`return`（`c6_return_value`）。要显式写出包装，可以用强转
`(closure int(int))fp`；反过来 `(int (*)(int))closure_value` 永远报 C91。

转换必须发生在 `@T` 的存储钩子**之前**：先把函数指针包成闭包值，再让
`vstore` 按托管引用做 +1/转移；顺序反了会把函数指针当对象去 incref。

**变参**：包装一个变参函数指针需要跳板把「未知的实参表」原样转发，C 做不到。
所以变参函数指针 → 闭包报 C92，提示写成捕获字面量（捕获字面量的体是直接
编译的，变参照常可用）。非捕获的**变参字面量**本身仍是裸函数指针；放进闭包
槽也会撞上 C92。

### 3.6 不透明性

闭包是语言级对象，不是结构体。用户只能：

* 调用 `f(args…)`；
* 拷贝/赋值/`move`/置 `NULL`/与 `NULL` 比较/布尔上下文；
* `sizeof`、`typeof`、`_Alignof`；
* 作为实参/返回值/字段/数组元素。

其它操作（`f->x`、`*f`、`f[i]`、`f + 1`、强转成 `void *` 或函数指针、
`offsetof`、`new`）一律 C97。编译器内部的调用改写直接读 `code`/`env`，
不经过成员访问的检查。

---

## 4. 捕获语义

### 4.1 隐式、按值、求值点快照

* 捕获集合 = 字面量体（含嵌套字面量的体、`defer`/`errdefer`/`catch` 的体）
  里**自由出现**、且在字面量写下的位置可见的块作用域**值**；
* 每个捕获在**字面量求值时**读一次、拷进 env。之后外层变量怎么改都不影响
  闭包；闭包里的改动也不影响外层；
* 同一个字面量表达式每求值一次就新建一套 env + 闭包对象。循环里写一次字面量
  = 每轮一个独立闭包；
* 捕获没有「引用」形式，也没有捕获列表；`[x, &y]` 这类语法不引入
  （`[` 已经属于下标/切片）。

### 4.2 能捕获什么

下表是**按值捕获**（`C6_CAP_VALUE`，会真的往 env 里拷一份）的规则：

| 按值捕获的对象 | 规则 |
|---|---|
| 标量、枚举、指针（含函数指针） | 按值拷贝 |
| `struct`/`union`（不含托管字段，且类型定义在文件作用域） | 按值拷贝（和普通赋值一样） |
| 切片 `T[:]` | 拷贝描述符；**不延长元素的寿命**（§4.6）；元素是 `@T` 也可以（切片不拥有元素） |
| 元组（元素都不是 `@T`） | 拷贝结构体；**不延长元素的寿命** |
| 元组含 `@T` 元素 | **不行**（C94）：按值拷贝会复制引用而不 +1 |
| `@T`（含 `shared @T`） | 存成 env 的强引用字段，+1（`shared` 用原子计数）；随 env 释放 |
| `closure S` | 同上（闭包也是 `@` 托管引用），+1 |
| `thread` / `task` 句柄 | **不行**（C94）：句柄带引用计数/析构，按值拷贝会破坏它；需要的话传句柄背后的值 |
| 托管形参 `@T p` | **可以**：读 p（借用）再 +1 存进 env，是安全的 |
| `const`/`volatile` 变量 | 拷贝；env 字段去掉顶层 `const`/`volatile`（存储用），体里的别名保留限定符，写 `const` 照常报错 |
| 块作用域 `static` | **不行**（C94）：把它移到文件作用域 |
| 块作用域函数声明（原型） | **不行**（C94）：把声明移到文件作用域 |
| 块作用域枚举常量 | **不行**（C94）：移到文件作用域 |
| 块作用域 `typedef`/tag | **不行**（C94）：类型不是值，体在 TU 末尾也看不见它 |
| 类型里含块作用域或匿名 struct/union/enum 的值 | **不行**（C94）：把类型定义移到文件作用域；块作用域的 tag 在 TU 末尾已经被弹出 |
| 方法接收者 `self`/`this` | **不行**（C94）：它是借用，闭包可能活过这次调用 |
| 带 `drop` 析构的 `struct` | **不行**（C94）：按值拷贝会让外层和 env 各析构一次 |
| 含柔性数组的 `struct` | **不行**（C94）：按值拷贝只复制固定部分，柔性部分会丢 |
| 数组、VLA（含指向 VLA 的指针） | **按值捕获不行**（C94）：数组不可赋值；VLA 的大小是运行期的。只在 `sizeof`/`typeof`/`_Alignof` 里出现的数组记 `C6_CAP_ZERO`（不拷贝）；VLA 仍 C94 |
| `own @T` | **不行**（C94）：独占引用不能复制 |
| `weak @T` | **不行**（C94）：弱引用不进入闭包 env（§7.3） |
| `weak @T` | **不行**（C94）：先升级成 `@T` 再捕获 |
| 含托管字段的结构体 | **不行**（C94）：C68 不允许按值拷贝，捕获 `@T` 持有者 |

「托管形参可以捕获」与 C70 不冲突：C70 禁止改写**形参槽**，而捕获是把借来的
值存进一个新槽（+1），这是 gc-design §5 明确允许的「先拷进自有局部」。

捕获值的 `CType` 必须**全局稳定**（`c6_type_global()`）：递归检查指针、
数组、函数和切片/元组/闭包里的每个 struct/union/enum tag，要求
`sym_scope == 0`；匿名 tag 定义在块里同样拒绝。函数类型的参数先按
`convert_parameter_type()` 归一（VLA 参数退化成指针），再看归一后的类型。
满足后，env 字段类型与体里的别名类型在 TU 末尾仍然有效；不满足就报 C94，
而不是让 `gen_function()` 去碰已经被弹出的 tag。

只出现在不求值上下文里的名字（`C6_CAP_ZERO`）**不受上表限制**：它不拷贝，
只要求 `c6_type_global()` 且不是 VLA。所以 `sizeof(含托管字段的局部)`、
`sizeof(块作用域 static)`、`sizeof(own @T 变量)` 都可以；真正去读/存它们才
报 C94。

### 4.3 体里的捕获名 = env 里的副本

在体里，捕获名就是一个普通的、可读写的左值，别名到 `env->__c6_c<N>`：

```c
void demo(void)
{
    int n = 1;
    closure int() f = int() {
        n = n + 1;      /* 改的是闭包自己的副本 */
        return n;
    };
    f();                 /* 2 */
    f();                 /* 3 */
    /* 外层的 n 仍然是 1 */
}
```

* 写、`++`/`--`、取地址 `&n`（得到 env 字段的地址）、`n.f = …` 都按普通
  **左值**处理：体里看到的是「`env + 偏移` 这个地址上的对象」，不是一份
  只读快照；`&n` 是裸指针，闭包释放后自然悬空（和任何字段地址一样）；
* `move(n)` **不支持**（`move` 只跟踪当前函数的自动托管槽，env 字段不是槽；
  动态左值没有可跟踪的符号）：要清空就写 `n = NULL`；
* 捕获的 `struct` 副本可以改字段：`p.x = 1;` 改的是副本；
* 嵌套字面量捕获外层闭包的捕获名时，是「读外层 env 字段的当前值，再拷进
  内层 env」——两层独立。

### 4.4 自捕获

```c
void demo(void)
{
    closure int(int) f = int(int n) { return n ? f(n - 1) : 0; };
    /* 错误 C98：字面量捕获了正在赋值的 f */
}
```

字面量求值时 `f` 还没有新值（右值先算，后写槽），捕获到的是旧值；如果旧值
是空，调用就崩。因此**直接赋给自己的字面量捕获自己**是硬错误 C98，提示用
具名函数做递归。间接的自引用（先 `f = NULL;`，再 `f = int(){ … g … };`，而
`g` 在某处又指回 f）无法静态判定，不在诊断范围内。

### 4.5 穿透嵌套字面量

外层字面量的捕获集合包含**嵌套字面量体里的自由名字**：

```c
void demo(void)
{
    int x = 1;
    closure int() outer = int() {
        return int() { return x; };  /* x 只出现在内层体里 */
    };
}
```

`x` 必须进外层 env，否则外层的体在 TU 末尾编译时，内层字面量没有来源可以
捕获。内层字面量自己的 env 只捕获「相对内层是自由」的名字：外层体自己的
局部量、形参，以及外层的捕获别名。

### 4.6 生命周期与借用（切片、裸指针）

* 捕获 `T[:]` 只拷贝 `{data, len}`，**不拥有元素**。切片的元素必须活得比闭包
  久；把指向局部数组的切片捕获进一个会逃逸的闭包，和把同样的切片存进
  `@struct`、传给 `async` 一样，是程序员的借用责任；
* 裸指针 `T *p` 按值拷贝；闭包不延长 pointee 的寿命。同理，体里取捕获变量的
  地址再返回，得到的是指向 env 内部的指针；闭包一释放就悬空，和返回指向
  局部变量的指针一样是用户责任；
* `@T`/`closure` 是唯一会「延长寿命」的捕获：env 持有强引用，值在闭包活着
  期间不会被释放；
* 闭包体执行期间 env 必然活着（调用者持有闭包的一份引用）；体里不能把 env
  本身存进别处（env 参数是 `void *`，不可见）。

### 4.7 与 `move`/借出/GC 的交互

* 捕获一个托管局部量**不是** `move`：外层槽仍持有它自己的引用，env 另 +1；
* 捕获一个已被 `move` 走的槽 → 命中 E-GC17（读已交出的槽），正常报错；
* 捕获一个托管形参 → 借用 → +1，安全（§4.2）；
* 闭包值本身可以 `move(f)`：把这份引用转移走，槽变空；
* 闭包字段/数组元素的释放由 GC 描述符统一处理，和普通 `@T` 字段一样。

---

## 5. 捕获分析（扫描 pass）

### 5.1 为什么不能在 TU 末尾做

字面量处的代码必须**在求值点**分配 env、把捕获值写进去；env 的字段集合与
偏移决定了这段代码。体是 TU 末尾才编译的，那时外层符号早已不在符号表里。
所以捕获集合必须在字面量处求出来。V1 的「否定表」只能回答「这个名字是不是
外层名字」，不能回答「体到底用了哪些」；V2 需要精确集合。

### 5.2 算法：用解析器扫一遍体

字面量处：

1. 解析头，得到用户签名 `ftype`（含参数名）；
2. `skip_or_save_block(&body)` 把体存成 `TokenString`（与 V1 同）；
3. 如果当前在函数里：`begin_macro(body, 1)` 重放，进入 **扫描模式**：
   * 保存 §5.4 列出的全部编译器状态；
   * 压一个哑符号 + 字面量的参数符号（从 `ftype` 的形参链 `sym_push_params()`
     得到，名字与类型都在），`local_scope = 1`；然后 `local_scope = 0`；
   * 把函数上下文设成「字面量的签名」：`func_vt` = 返回类型、`func_var`、
     `func_old`、`c6_func_sym`（只为 `try` 判断 `!error`）、`funcname`、
     `c6_async_cur = 0`、`c6_gc_in_drop = 0`；`func_vla_arg()` 照常算 VLA 形参；
   * `nocode_wanted = 1`（正数：代码与静态数据都不发，`NODATA_WANTED` 为真）；
   * `block(0)` 走完体；
   * 弹出参数/哑符号，恢复 `loc`、`vtop` 与 §5.4 的状态；
   * `end_macro()`；
4. 得到 `C6Lit.caps`：按**首次出现的源代码顺序**排好的捕获表；
5. 建 env 类型、发分配代码、排队体（§6）。

扫描用的是同一个 `block()`/`unary()`/`decl()`，所以遮蔽、作用域、`for` 初始化、
`switch`、标签、`defer`、`catch`、嵌套字面量全部按真实语义处理；不需要写第二
个 C 解析器，也不会漏。

### 5.3 符号序号：谁是「体内的」

`Sym` 增加一个 `int c6_sym_seq`（在 `sym_push2()` 里从 TU 全局计数器领取）。
扫描开始时记下 `scan_base = ++c6_sym_seq`。任何一次值解析命中符号 `s` 时：

```
s->c6_cap_kind != 0                       → 外层字面量的捕获别名，按别名种类
                                            （VALUE/TYPE/ZERO）记录
s->sym_scope == 0                         → 文件作用域，不捕获
s->c6_sym_seq >= scan_base                → 体内声明的（参数、局部、嵌套体的
                                            参数/局部），不是本次捕获
否则（块作用域且创建于字面量之前）        → 捕获候选，按 §4.2 分类
```

这比「按名字判断」精确：体内的同名声明、`for (int i = …)`、嵌套字面量的
参数都会正确地遮蔽外层名字；引用一个稍后才在体内声明的名字时，按 C 的规则
解析到外层（因此正确地成为捕获）。

### 5.4 扫描模式必须抑制的副作用

扫描是「只解析、不产出」，但 c6 的很多特性在解析时会改全局表。用
`c6_lit_scanning`（深度计数）在下列入口短路；`nocode_wanted = 1` 已经挡住了
代码和静态数据，剩下的表要显式挡：

| 子系统 | 扫描时的行为 |
|---|---|
| 字面量队列 `c6_lit_head/tail/count` | 嵌套字面量不入队、不领 `__c6_fn_N` 名字；只递归扫它的体 |
| `defer_state`（`c6_defer_parse`/`c6_defer_begin`） | 只解析 `defer` 后面的语句（名字要进捕获集合），不登记记录、不生成调度 |
| `errdefer`/`err`/`try`/`catch` | 照常解析语义与体；不生成错误路径代码 |
| `c6_gc_*`（`c6_gc_site`、`c6_gc_root_add`、`c6_gc_arg_*`、描述符队列） | 不登记、不发描述符；`new` 只做类型检查 |
| `spawn`（`c6_sp_pending`） | 只解析实参，不登记跳板 |
| `co`/`task`/`thread` | 只解析，不生成任务/句柄代码 |
| 方法/接口注册（`c6_method_register`、`c6_auto_impl_ensure`） | 不登记（体内定义的结构体没有持久方法） |
| `c6_hdr_*`、`c6_module_*` | 体里不可能出现，防御性短路 |
| 标签栈 | 压一层新标签作用域；外层的标签在扫描时不可见（体内 `goto` 外层标签因此和真实编译一样报错） |
| 匿名符号/字符串常量的分配 | `nocode_wanted = 1` 下 `NODATA_WANTED` 为真，`put_extern_sym`/`section_add` 不发 |
| 切片/元组/闭包类型的 intern 表 | **不特殊处理**：它们都挂在 `global_stack` 上，且元素/签名已按全局稳定规则检查过（§4.2），扫描建过的类型真实编译直接复用，不会留下悬空的块作用域 tag |
| `anon_sym`、类型计数器 | 允许递增（名字只是跳过几个号，无副作用） |

除此之外，所有函数级/解析级全局状态都成对保存、恢复：`func_vt`、`func_var`、
`func_old`、`func_vla_arg`、`rsym`、`nb_temp_local_vars`、`c6_func_sym`、
`c6_async_cur` 及其 `c6_async_*`、`defer_state`、`c6_decl_fallible`、
`c6_try_*`、`c6_implicit_ret0`、`c6_last_stmt_leaves`、`local_scope`、
`local_stack`、`local_label_stack`、`cur_scope`、`root_scope`、`loc`、`vtop`、
`nocode_wanted`、`global_expr`、`c6_self_*`、`c6_iface_*`、`c6_await_pending`、
`c6_fstr_inexpr`、`c6_gc_init_store`、`c6_gc_no_hook`、`c6_gc_in_drop`、
`c6_async_in_body`、`c6_lit_cur`、`c6_lit_env_sym`、`c6_lit_body_base`、
`c6_lit_replay`、`c6_scan_uneval`。
扫描用一个 `C6ScanSave` 结构体集中管理，进入/退出各一次。`c6_sym_seq`
**不恢复**：扫描建的符号已经把号用掉，恢复会让后面的真实符号复用序号，
破坏 `seq >= base` 判断。

`c6_lit_scanning > 0` 时 `c6_lit_cap_check()` 直接返回：扫描记录捕获，不再
按 V1 报错。

### 5.5 嵌套字面量

扫描遇到嵌套字面量时：

* 解析它的头（得到签名、参数名），`skip_or_save_block` 存下它的体（随后丢弃）；
* **不**建立 `C6Lit`、不入队、不分配函数符号；
* 重放它的体，继续用**外层字面量的 `scan_base`** 扫描。于是：
  * 引用外层体局部/形参（`seq >= scan_base`）→ 不是外层捕获（内层自己捕获，
    真实编译时再算）；
  * 引用外层字面量的捕获源（`seq < scan_base`）→ 记进外层捕获；
* 扫描时嵌套字面量的**值类型按 V1 的函数指针**给（因为捕获集合未定）。
  这只影响被丢弃的扫描代码：函数指针 → 闭包的包装转换在扫描里是允许的，
  所以 `closure S g = int(){…};`、`g(...)`、`return literal;` 都能通过；
  真正的类型诊断（C93 等）在 TU 末尾真实编译体时给出。

嵌套扫描也把 `c6_lit_env_sym`/`c6_lit_replay` 保存/恢复。关键：**扫描模式下
`unary()` 的 cap 分支不碰 env**，一律压 `VT_CONST|VT_LVAL,0` 的假左值
（类型 = 别名类型），只为类型检查。这样内层扫描既不需要内层的 env 参数，
也不会误用外层的 `c6_lit_env_sym`；`nocode_wanted` 保证不出代码。同时把
`func_vt`/`func_var`/`c6_func_sym` 切到内层字面量的签名（一个临时 Sym），
这样内层体里的 `return`/`try`/`catch` 按内层返回类型检查；扫完恢复外层的
上下文。

### 5.6 环境类型与捕获顺序

* 捕获表按**首次出现的源代码顺序**排列（外层体、嵌套体、`defer` 体、`catch`
  体一起按 token 顺序）；同一符号只出现一次。同一 token 先记 `ZERO` 后又在
  求值位置出现，就**升级为 `VALUE`**（env 加字段）；同一 token 只可能对应
  字面量处可见的同一个外层符号，不会既是 TYPE 又是 VALUE；
* 环境字段只给 **VALUE** 条目编号：`__c6_c0, __c6_c1, …` 按 VALUE 在捕获
  表里的出现顺序；TYPE/ZERO 不占字段、`off` 为 0。用 `struct_layout()` 算
  偏移/大小/对齐（`long double`、向量类型的对齐也要对），偏移存进
  `C6Lit.caps[i].off`；env tag 挂在 `global_stack` 上；
* `C6Cap.type` = 捕获符号的类型，但**清掉 `C6_GQ_BORROW`**：env 字段拥有
  一份引用，体里的别名也是拥有左值，不是借用（捕获托管形参时尤其重要）；
* env 字段类型 = `C6Cap.type` 再去掉顶层 `const`/`volatile`（存储用，否则
  初始化会被 const 检查挡住）；别名符号保留 `C6Cap.type` 的顶层限定符，
  所以体里对 `const` 捕获赋值仍报错；
* 环境类型在**全局作用域**注册（tag `__c6_env_<n>`），这样 TU 末尾编译体时
  它仍然可见，描述符也能发；
* 捕获表里区分三种条目：
  * `C6_CAP_VALUE`：正常捕获，env 有字段、字面量处初始化；
  * `C6_CAP_TYPE`：被引用的名字是块作用域的 `typedef`。env **不放字段**，
    体里压一个同名 `VT_TYPEDEF` 别名指向捕获下来的 `CType`，所以
    `sizeof(T)`、`T x;` 都能编译；类型本身必须全局稳定（§4.2），否则 C94；
  * `C6_CAP_ZERO`：一个**值**的名字只出现在不求值的上下文
    （`sizeof`/`typeof`/`_Alignof`）里。env 不放字段，体里压一个该类型的
    **零地址假左值**（`VT_CONST|VT_LVAL`、地址 0），只为 `sizeof`/`&`/`x.f`
    这些类型检查能过，不产生代码、也不拷贝。数组靠这一条支持
    `sizeof(外层数组)`；VLA 仍报 C94（大小是运行期的，`sizeof` 一个 VLA
    也一样）。ZERO 不拷贝，所以不受 §4.2 的按值捕获限制，只要求类型全局
    稳定。

`lit->capturing = (存在 C6_CAP_VALUE)`：只有 TYPE/ZERO 条目不建 env、不算
捕获式闭包；`c6_lit_push_captures()` 仍然要为它们压别名（否则体在 TU 末尾
编译时找不到那个类型/名字）。

扫描维护一个 `c6_scan_uneval` 深度：`sizeof` / `typeof` / `_Alignof` 的
操作数进入时 ++、退出时 --；记录一个**值**名字时据此选 `C6_CAP_ZERO`
（深度 > 0）还是 `C6_CAP_VALUE`（深度 == 0）。例外：**VLA 维度表达式**
（`int a[n]`、`sizeof(int[n])` 里的 `n`）是运行期求值的，解析它时把深度
临时清零，`n` 记 `C6_CAP_VALUE`；`sizeof(a)` 里的 `a` 本身是 VLA 时仍报
C94。

### 5.7 正确性保证

* 扫描与真实编译共用解析器，遮蔽/作用域完全一致；
* 真实编译体时，捕获别名先于参数压栈（§6.3），体里任何对外层名字的引用都
  必须命中别名；若没有命中，说明扫描漏了——真实编译会在 TU 末尾报
  `'x' undeclared`。为了让这种内部错误可诊断，`c6_lit_replay` 且正在编译
  捕获体时，值解析未命中别名、但该 token 在捕获表里存在 → 报内部错误
  `capture analysis missed 'x'`（正常情况下不可达）；
* 扫描时多记了名字（例如类型用途）只会让 env 多一个字段或一条 TYPE 条目，
  不会改变语义；少记通常会在真实编译暴露成 `'x' undeclared`。唯一的盲区是
  「外层局部量遮蔽了同名全局量、而扫描漏记」：TU 末尾会静默解析到全局量。
  所以扫描必须和真实解析完全同源，不能靠事后补；测试里的遮蔽用例就是钉这个；
* 测试里专门放一组「遮蔽」用例（§13），把「按名字扫描」会错的写法全部钉住。

---

## 6. 生成

### 6.1 字面量处的发射序列

设字面量捕获了 `n`（int）和 `p`（`@Node`），签名是 `int(int)`：

```c
/* 1. 环境 */
struct __c6_env_7 *env = __c6_gc_new(sizeof(struct __c6_env_7), 1, &env_desc, 0);
env->__c6_c0 = n;                 /* 普通拷贝 */
env->__c6_c1 = p;                 /* 托管字段：+1（借来的 p 也照常 +1） */

/* 2. 闭包对象 */
struct __c6_closure_<h> *cl =
    __c6_gc_new(sizeof(struct __c6_closure_<h>), 1, &closure_desc, 0);
cl->__c6_code = __c6_fn_3;        /* 符号类型与字段类型完全相同 */
cl->__c6_env = env;               /* FRESH：所有权转给闭包，不再 +1 */
                                  /* 静态类型转成 @__c6_env_base *（内部擦除） */

/* 3. 值 */
f = cl;                           /* FRESH：转移进 f；vstore 接管 */
```

实现要点：

* 两个指针各用一个隐藏的 `VT_LOCAL` 槽（`loc` 里切），只在本表达式内使用；
* env 的字段访问用 `env + off` 再 `indir()` 得到左值；初始化走 `vstore()` 的
  托管钩子（`C6_GC_INIT`），因此 `@T` 捕获自动 +1；
* `cl->__c6_env = env` 用 `C6_GC_FRESH|C6_GC_INIT` 存储（或 `c6_gc_no_hook`
  后直接写），env 的引用计数 1 直接转交，不额外 +1；
* 结果值 `vtop->gc_state = C6_GC_FRESH`，交给现有的 FRESH 消费/丢弃逻辑；
* 内部只把闭包/env 指针当普通地址用时（`env + off`、`gaddrof`、`cl->code`），
  用 `c6_gc_plain()` 擦掉托管标记，避免误触 `vstore`/`gaddrof` 的托管钩子；
* `c6_gc_arg_temp()`/`c6_gc_arg_release()` 对闭包实参同样生效（它就是一个
  `@T` 值）；
* 没有 `C6_CAP_VALUE` 时**完全走 V1**：不建 env、不建闭包，值就是函数
  指示符（TYPE/ZERO 别名仍然压进符号表供体编译）。

### 6.1.1 表达式压栈与推断声明

`c6_lit_parse()` 在扫描之后才决定值形态，所以 `vtype` 与压栈放在最后：

* `capturing == 0`（没有 `C6_CAP_VALUE`）：
  `vtype = lit->ftype; vpushsym(&vtype, lit->sym)`（V1 原样）；
* 非空：按上面的序列发完代码，`vtype = lit->closure_type`，从隐藏槽按
  `VT_LOCAL|VT_LVAL` 载入闭包指针；`vtop->gc_state = C6_GC_FRESH`。

推断声明 `c6_lit_infer_decl()`：非空时 `vtop->type` 已经是 `closure` 托管
引用，**不能再 `mk_pointer`**（否则会变成指向闭包引用的指针）；槽类型就是
它，`vstore` 走 `@T` 钩子。空时仍按 V1：`vtop->type` 是函数类型，
`mk_pointer` 之后退化成函数指针，再切普通槽。
已有声明的赋值不走推断，按 §3.5 检查（捕获字面量 → 函数指针是 C93）。

`sizeof`/`typeof` 上下文里 `c6_lit_parse()` 仍完成扫描和类型判定（值类型
不依赖求值），但在 `NOEVAL_WANTED` 下**不分配环境/闭包、不占隐藏槽**：直接
压一个值类型的零值（`vpushi(0)` + `vtop->type = vtype`），只给
`sizeof`/`typeof` 取类型；`sizeof` 的结果是 `sizeof(void *)`。

如果闭包值——捕获字面量，或函数指针 → 闭包的包装——处在文件作用域或
`static` 局部的初始化式里（`global_expr` / `DATA_ONLY_WANTED`），它在运行期
没有可以执行分配的位置，报 C95；`= NULL` 仍然可以。

### 6.2 代码符号与类型

* 用户签名 `ftype` = `RET (P…)`。签名必须**全局稳定**：字面量函数在 TU
  末尾发射，形参/返回类型里的块作用域 tag 到那时已经不可见。这条对**所有**
  字面量生效（V1 也有同样的底层限制，只是没诊断）；V2 在 `c6_lit_parse()`
  里统一检查，报 C90 的第三条消息；
* 代码签名 `ftype_code` = `RET (void *, P…)`：在参数链**最前面**插一个无名
  `void *` 参数（返回类型、变参位、`!error` 位不变）；
* 符号在扫描之后才发布（捕获与否决定类型）：
  `lit->sym = external_sym("__c6_fn_N", lit->capturing ? &ftype_code : &ftype,
  VT_STATIC|VT_EXTERN, …)`；
* 扫描期间用 `lit->sym` 指向一个**临时** Sym（类型 `ftype_code`，压在
  `local_stack` 上、不 `external_sym`），只为 `try`/`return` 判断 `!error`
  和返回类型；扫描结束弹掉临时符号，再发布真符号。
* 闭包描述符里保存 `ftype`（用户签名）与 `ftype_code`（代码字段的类型）；
* 闭包类型的 `code` 字段类型就是 `ftype_code` 的函数指针，**不丢 env 参数**：
  调用点用 `c6_self_pending` 把 env 作为第一个显式实参插进去，通用调用代码
  再从 `sa = ftype_code->next` 继续消费用户实参。这样：
  * 类型检查正确（env 按 `void *` 检查）；
  * 方法/接口的 self 机制原样复用；
  * sret 缓冲在 env 之前（通用调用代码本来就先放 sret）；
  * 变参：env 是固定参数，`va_start` 在它之后，正确。

### 6.3 体的发射与捕获别名

TU 末尾 `c6_lit_flush()` → `c6_lit_emit()` → `gen_function(lit->sym)`。
`gen_function` 在压完哑符号后、压参数前做两件事：

1. `c6_lit_push_captures(lit)`：按条目种类压别名（只为**名字解析/遮蔽**，
   VALUE 的值不由别名本身提供）：
   * `C6_CAP_VALUE`：token = 原名字，类型 = 捕获类型（顶层 `const` 保留，
     体里写它照常报错），`r = VT_CONST`、`c = 字段偏移`、
     `c6_cap_kind = VALUE`；
   * `C6_CAP_ZERO`：token = 原名字，类型 = 捕获类型，`r = VT_CONST`、`c = 0`、`c6_cap_kind = ZERO`；
   * `C6_CAP_TYPE`：token = 原名字，`type = 捕获下来的 CType`、
     `t = VT_TYPEDEF`、`c6_cap_kind = TYPE`（让 `parse_btype`
     按 typedef 找到它）；
   * 三种都用函数作用域 `sym_scope = 1`（和参数/体最外层同级）。别名是
     编译器生成的，`sym_push()` 的 redeclaration 检查要放行
     `prev_tok->c6_cap_kind != 0`：同名参数或局部**遮蔽**别名，但参数和局部之间
     照旧按 C 报重复定义。`c6_lit_cap_check()` 用 `c6_cap_kind` 识别别名。
     序号小于随后设的 `body_base`。
2. 设 `body_base = ++c6_sym_seq`，再 `sym_push_params()` 压用户参数。
   **`sym_copy()` 会把形参 Sym 的旧 `c6_sym_seq` 一起拷过来**，所以
   `sym_push_params()` 每复制一个参数都要重新发号
   （`p->c6_sym_seq = ++c6_sym_seq`）；否则参数的序号小于 `body_base`，
   会被 `c6_lit_cap_check()` 当成外层捕获。如果 `lit->capturing`，参数链
   最前面的就是无名的 env 参数，找到它并存进 `c6_lit_env_sym`；否则不设
   （没有 VALUE 条目需要它）。参数在别名之后压，所以同名参数自然遮蔽别名；
   参数的序号大于 `body_base`，嵌套字面量扫描时不会被误当作外层捕获。

`unary()` 的值解析路径在 `s = sym_find(t)` 之后、`vset(&s->type, r, s->c)`
之前增加一个分支：

```
if (s && s->c6_cap_kind != 0) {
    if (kind == TYPE) {
        /* 值位置：留给普通 typedef 诊断；类型位置由 parse_btype 处理 */
    } else if (c6_lit_scanning) {
        /* 扫描模式：不碰 env，只给类型检查一个假的零地址左值 */
        vset(&s->type, VT_CONST | VT_LVAL, 0);
        vtop->sym = NULL;
        break;
    } else if (kind == ZERO) {
        /* 只用于 sizeof/typeof/_Alignof：压一个假的零地址左值，
           让 &x、x.f、x[0] 这些类型检查能过，但不产生任何代码 */
        vset(&s->type, VT_CONST | VT_LVAL, 0);
        vtop->sym = NULL;
        break;                           /* 跳出 switch，进入后缀处理 */
    } else { /* VALUE */
        /* env 参数是隐藏的 void * 首参；压它，加字段偏移，再 indir()：
           得到的是 env 字段上的**动态左值**，读/写/&/++/x.f 全都自然工作 */
        c6_lit_env_push();               /* vset(env_sym, VT_LOCAL|VT_LVAL) */
        vtop->type = char_pointer_type;
        vpushi(s->c); gen_op('+');
        { CType pt = s->type; mk_pointer(&pt); vtop->type = pt; indir(); }
        vtop->type = s->type;
        break;
    }
}
```

* `c6_lit_env_push()` 在 `nocode_wanted` 下只压符号、不生成代码，所以
  `sizeof(捕获名)` 不会真的算地址；
* 捕获的结构体：`x.f`、`x.f = …` 都作用在 `env->field` 左值上；
* 捕获的闭包/函数指针：`x(args)` 走各自的调用路径；
* `&x` 得到 env 字段的地址（裸指针）；
* 捕获名在体里的**类型**就是捕获类型，诊断里显示原名。

`c6_lit_cap_check()` 在 V2 保留为「类型捕获」防线：先看 `s->c6_cap_kind != 0`
（别名，放行）；如果体里解析到块作用域的 `typedef`/tag（`seq < body_base`），
报 C94；值路径正常应命中别名。

### 6.4 调用点改写

`f(a, b)`：

```
tmp  = f;                       /* 借用，不计数；放进隐藏槽 */
env  = tmp->__c6_env;           /* 借用 */
c6_self_loc = env;              /* 复用方法/接口的隐藏 self 槽 */
c6_self_type = void *;
c6_self_pending = 1;
code = tmp->__c6_code;          /* 函数指针，类型 ftype_code */
… 通用调用代码：先插 sret，再插 env，再消费 a, b …
```

* `c6_self_pending` 在**解析实参之前**就把 env 压进参数序列（和接口/方法
  完全一样），所以实参里再嵌套一个闭包调用也不会覆盖外层的 env；
* 闭包表达式只求值一次（先存进隐藏槽），`f` 是临时值时也安全；
* 隐藏槽里放的是**借用**：用 `c6_gc_no_hook` 直接存，不 +1，调用结束也不
  release；`env`/`code` 的载入同理；
* 结果按代码签名的返回类型处理：`!error` 位来自 `ftype_code`，`try f()`
  照常工作；托管返回值按现有规则标记 FRESH；
* 调用 NULL 闭包 = 解引用空指针（和 NULL `@T` 一样，运行期陷阱）。

### 6.5 包装函数指针（跳板）

`closure S c = fp;`（`fp` 是非变参函数指针，或非捕获字面量）：

```c
struct __c6_env_ptr_<h> *envp =
    __c6_gc_new(sizeof(struct __c6_env_ptr_<h>), 1, 0, 0);
envp->__c6_fn = fp;                       /* 普通函数指针字段，非托管 */

struct __c6_closure_<h> *cl =
    __c6_gc_new(sizeof(struct __c6_closure_<h>), 1, &closure_desc, 0);
cl->__c6_code = __c6_tramp_<h>;           /* 每个签名一份，TU 末尾发 */
cl->__c6_env = envp;
```

```c
static RET __c6_tramp_<h>(void *__c6_env, P0 a0, P1 a1, …)
{
    struct __c6_env_ptr_<h> *e = (struct __c6_env_ptr_<h> *)__c6_env;
    return e->__c6_fn(a0, a1, …);
}
```

* 跳板按**规范签名**去重：同一签名只发一份，和 `c6_sp_flush()` 的挂起表
  同一手法（`C6TrampPending` 链表，TU 末尾发）；
* 跳板的定义类型就是 `ftype_code`，闭包 `code` 字段直接指它，无强转；
* `!error` 返回类型：跳板照常返回值，错误槽由被调函数设置，调用点的
  `try` 看得到；
* struct 返回：跳板是普通 struct 返回函数，调用点的 sret 在 env 之前，
  与直接调用一致；
* 变参：C92（无法转发未知实参表）；捕获字面量不受影响（它的体直接编译）。

包装是隐式转换，发生在 §3.5 的四个消费点。`gen_assign_cast()` /
`vstore()` 里的顺序必须是：**先做闭包转换，再走 `@T` 的存储钩子**——否则
存储钩子会把一个函数指针当成托管对象去 incref。

### 6.6 ABI 细节

| 情形 | 结果 |
|---|---|
| 普通参数 | env 是第一个显式实参（sret 缓冲之后），与方法 `self` 完全同构 |
| struct 返回 | sret 指针 → env → 用户参数；`c6_self_pending` 本来就插在 sret 之后 |
| 变参闭包 | `(void *, 固定参数…, ...)`；`va_start` 跳过 env；调用点照常传 env |
| `!error` | 代码字段的函数类型带 `func_fallible`，`try`/`catch` 照常识别 |
| 托管返回 | 调用点标记 FRESH；`return p` 的 +1 由现有返回钩子负责 |
| 32 位/ARM/RISC-V/PE | env 占第一个参数位；与方法/接口的 self 走同一条后端路径 |
| C99 后端 | env 是普通首参；跳板/描述符都是普通 C 代码（§8.2） |

调用点从 `tmp->__c6_code` 载入函数指针时，`vtop->sym` 为空，不要让它误触
方法/`drop`/`async` 的符号特判。

---

## 7. GC、线程与环

### 7.1 描述符与释放

* 闭包对象：`@struct __c6_closure_<hash>` 的描述符由现有 `c6_gc_collect()`
  生成：`__c6_code` 不是托管字段，`__c6_env` 是 `STRONG` 字段（被指类型
  `__c6_env_base`）；闭包死亡 → 释放 env → env 计数归零 → 按 env 描述符
  释放捕获的 `@T` 字段；
* 环境对象：描述符只列**直接托管捕获**（`@T`、`closure`），类型 `STRONG`。
  捕获的切片/含托管字段的结构体本来就不能拷贝，不会出现在描述符里；没有
  托管捕获时描述符指针为 0（仍需分配对象头以便统一释放）；
* 描述符在 TU 末尾由现有 `c6_gc_flush_descs()` 发射；闭包/环境是全局作用域
  的合成结构体，不触发 C74（块内定义）；
* 闭包值的所有权完全走 `@T`：作用域退出释放、赋值 +1/−1、`return` 转移、
  FRESH 实参在调用后释放、`move`、`NULL`、全局根（文件作用域 `closure S g`
  会被 `c6_gc_root` 登记，进程退出释放）；
* **不新增运行期函数**：只用 `__c6_gc_new/store/release` 和现有描述符机制。
  跳板是普通 `static` 函数。

### 7.2 静态环检测（C66）

env 在静态图里是「擦除」的，不能直接建 `closure → env_N → 捕获类型` 的边
（env_N 是每字面量一个、跨 TU 不稳定）。改为**保守的签名级边**：

```
对每个使用签名 SIG 的字面量，对每个被捕获的托管类型 T：
    边  closure_<SIG>  →  T
```

* `T` = 捕获的 `@Node` 的被指类型 `Node`；捕获 `closure S2` 时 `T` 就是
  `closure_<S2>` 本身；捕获标量/切片/裸指针不建边（切片不拥有元素）；
* 这样 `struct Node { closure int() f; }` 被某个 `closure int()` 捕获时，
  图里出现 `closure_int_void → Node → closure_int_void`，报 C66；
* 边是**保守**的：同一签名的另一个字面量即使不捕获 Node，也会参与这条边。
  这与 C66 现有的类型级保守性一致（`struct Node { @Node next; }` 无论是否
  真的成环都报错）；
* 闭包节点用签名哈希命名，环境节点不进入图；单 TU 和跨 TU 都使用同一套
  `closure_<hash>` 名字，所以跨 TU 环也能查；
* 签名里的嵌套闭包递归建边。

运行时环回收仍然不做（gc-design 的非目标）。

### 7.3 线程与任务（V2.0 明确禁止）

* `shared`/`weak`/`own` 修饰闭包 → C96。原因：`shared` 需要在**分配点**把
  闭包对象与 env 的计数改成原子；字面量的静态类型在分配点之后才由上下文
  决定，无法回头改对象头。`weak`/`own` 的语义也没设计好；
* 闭包作为 `spawn` 实参、`async` 参数/返回值、`co`/`task` 的载荷 → C96
  的第二条消息（「闭包不能跨线程/任务」），比通用的 C73 更明确（C73 会
  建议用 `shared`，而 V2.0 没有 `shared` 闭包）；
* 捕获 `shared @T` 是允许的：env 本身单线程，字段指向跨线程对象，释放时按
  目标对象头的原子标志走；
* 闭包单线程的生命周期规则因此很简单：创建、调用、释放都在同一个线程。

### 7.4 无 STW、预算

沿用 gc-design：引用计数、每线程待释放队列、预算化 drain。闭包/环境只是
普通对象，不引入新的阻塞点。一次闭包求值最多分配两个对象；释放闭包可能
级联释放 env 及其捕获的 `@T`（和普通字段释放一样）。

---

## 8. 后端与互操作

### 8.1 原生后端

* 闭包对象/环境对象的分配走 `__c6_gc_new`（和 `new` 同一条运行期路径）；
* 字段读写是普通的地址 + 偏移 + 载入/存储，没有新的后端指令；
* 调用点复用 `gfunc_call` + `c6_self_pending`（方法/接口已验证的路径）；
* 跳板是普通 `static` 函数，走 `gen_function`；
* 五个已知目标（x86_64、i386、arm、arm64、riscv64、PE）都只需要前端改动；
* `c67` 后端不在 V2.0 的验收范围；如果它没有完整的托管引用/GC 路径，
  遇到 `closure` 应报「this backend does not support closures yet」，
  不静默生成错误代码。

### 8.2 C99 输出后端

`-c99` 输出的是可读 C，闭包不能变成「不可读的运行期魔术」：

* 在 C 输出的声明区发射用到的闭包结构体：

  ```c
  struct __c6_closure_<hash> {
      RET (*__c6_code)(void *__c6_env, P…);
      void *__c6_env;                 /* 与 c6 的 @__c6_env_base * 同布局 */
  };
  ```

  类型打印成 `struct __c6_closure_<hash> *`（托管引用在 C 里就是指针）。
  结构体前后加 `#pragma pack(push, 8)` / `#pragma pack(pop)`，用户代码里的
  `#pragma pack` 不能改它的布局；
* 环境结构体**不需要**出现在 C 输出里：分配只传字节大小，捕获读写用前端算好
  的偏移，生成代码不按 C 字段名访问；
* 描述符是普通的静态数据（现有 GC 描述符发射路径），C 里就是结构体常量；
* 跳板是普通函数；调用点是普通调用，首参 `void *`；
* 变参、struct 返回、`!error` 都由前端处理成普通 C 调用；
* `tests/c99/closure.c6` 用 `gcc -std=c99 -pedantic -Wall -Wextra -Werror`
  编译生成代码，钉住「输出没有依赖 tcc 的扩展语法」。

### 8.3 `-emit-header`、`module`、`import`、跨 TU

`-emit-header` 的公开声明里可能出现闭包类型：

```c
struct __c6_closure_<hash> {
    int (*__c6_code)(void *__c6_env, int);
    void *__c6_env;
};

void each(struct __c6_closure_<hash> *cb);      /* closure void(int) */
struct __c6_closure_<hash> *make(int n);        /* closure int(int) */
```

* 闭包结构体在函数原型之前发射，每个哈希一次；`__c6_env_base` 不必导出
  （env 字段写成 `void *`）。结构体用 `#pragma pack(push, …)`（或等价的
  `_Alignas`）钉住 `2*PTR_SIZE` 布局，包含方自己的 `#pragma pack` 不能改它；
* 结构体返回/切片/元组参数按现有 `c6_hdr_type` 递归，先发它们需要的
  typedef，再发闭包结构体；
* `typedef closure int(int) IntOp;` → `typedef struct __c6_closure_<hash> *IntOp;`；
* `module` 不改写 `__c6_` 前缀的名字；`import` 重放原型时，同一签名重新得到
  同一个哈希名，所以跨 TU 的函数类型兼容；
* `-emit-header` 只导出 `module` 的公开声明；字面量函数、env 类型、跳板都是
  `static`，不导出。

### 8.4 `test`、`-run`、调试

* `tests/*.c6` 的 test 块里可以定义/调用闭包；`c6_lit_flush()` 在 TU 末尾与
  `gen_inline_functions()` 同一循环里排空队列，所以 test 体、`static inline`
  体里排队的闭包也会被编译；
* `-run`/JIT 使用同一套静态数据与描述符，无需特殊处理；
* 调试信息里看到的内部名是 `__c6_fn_N`；env 类型名是 `__c6_env_N`，都不是
  用户 API。

---

## 9. 诊断（C90–C98）

编号接在 f-string 的 C89 之后。**这些诊断随 V2 一起生效**；V1 的字面量仍按
C80–C83 报（其中 C80 在 V2 下只剩「没有按值捕获、却引用了块作用域名字」）。

| 编号 | 条件 | 消息 |
|---|---|---|
| C90 | `closure` 后不是完整函数签名；签名里用了匿名/块作用域类型（**对所有字面量**，不只 closure 类型）；`@closure` | `a 'closure' type needs a full signature: 'closure RET (parameters)'`；`a closure signature cannot use an anonymous 'struct'/'union'/'enum': give it a tag`；`a closure signature cannot use a type declared in a block`；`a function literal signature cannot use an anonymous 'struct'/'union'/'enum': give it a tag`；`a function literal signature cannot use a type declared in a block`；`'@' cannot be applied to a closure type: a closure is already a managed reference` |
| C91 | 闭包 → 函数指针；闭包 → `void *`；闭包签名不匹配的显式转换 | `a closure cannot become a plain function pointer: it carries an environment (call it, or keep it as 'closure RET (parameters)')`；`a closure cannot be converted to '%s': closures are opaque` |
| C92 | 变参函数指针/变参非捕获字面量 → 闭包 | `a variadic function pointer cannot be wrapped into a closure: a trampoline cannot forward an unknown argument list (make it a capturing literal instead)` |
| C93 | **按值捕获**（有 `C6_CAP_VALUE`）的字面量出现在需要普通函数指针的位置 | `this literal captures '%s', so its value is a closure, not a function pointer: use 'closure RET (parameters)' as the type, or pass the value in as a parameter` |
| C94 | 捕获了不能捕获的名字/类型 | 见下面的分条消息 |
| C95 | 闭包值出现在文件作用域/`static` 局部的初始化式里（除 `NULL`） | `a closure is not a constant expression: initialize it at run time ('NULL' is fine at file scope)` |
| C96 | `shared`/`weak`/`own` 修饰闭包；闭包跨线程/任务 | `a closure cannot be 'shared', 'weak' or 'own': V2 closures are confined to the thread that creates them`；`a closure cannot cross a thread or a task: V2 closures are confined to the thread that creates them` |
| C97 | 对闭包做不透明操作（`->`、`*`、`[]`、算术、`new`、`offsetof`） | `'%s' is a closure: it can only be called, compared with NULL, copied, moved or released`；`a closure cannot be created with 'new': use a function literal or a function pointer` |
| C98 | 直接赋给自己的字面量捕获自己 | `'%s' is captured by the closure being assigned to it: the closure would see the old value (use a named function for recursion)` |

C94 的分条消息：

```
a closure cannot capture the type '%s': types are not values
    (move the typedef or tag to file scope)
a closure cannot capture '%s': its type is declared in a block
    (move the type to file scope)
a closure cannot capture the method receiver '%s': it is a borrow and the
    closure may outlive the call (capture the values you need, or pass it
    as a parameter)
a closure cannot capture the block-scope 'static' '%s': move it to file scope
a closure cannot capture the function '%s' declared in a block: move the
    declaration to file scope
a closure cannot capture the constant '%s': move it to file scope
a closure cannot capture the array '%s': pass a slice 'T[:]' or a pointer instead
a closure cannot capture the variable length array '%s': its size is only
    known at run time
a closure cannot capture '%s': an 'own' reference cannot be copied
a closure cannot capture '%s': upgrade the 'weak' reference to '@T' first
a closure cannot capture '%s': it holds managed references and cannot be
    copied by value (capture the '@T' owner instead)
a closure cannot capture '%s': a thread/task handle owns a resource and
    cannot be copied by value
a closure cannot capture '%s': the type has a 'drop' destructor, so a copy
    would be destroyed twice (capture a '@T' owner instead)
a closure cannot capture '%s': it is a tuple with a managed element, so a
    by-value copy would duplicate the reference (copy it into a '@struct')
a closure cannot capture '%s': it has a flexible array member, so a by-value
    copy would drop the flexible part
```

V1 的 C80（「不能捕获」）在 V2 下拆成：

* 捕获**值**且上下文是闭包 → 允许；
* 捕获**值**且上下文是普通函数指针 → C93；
* 捕获**类型/不能携带的值** → C94。

---

## 10. 与其它特性的交互

| 特性 | 交互 |
|---|---|
| `!error` / `try` / `catch` | 闭包签名可带 `!error`；`try f()` 通过代码字段的 `func_fallible` 识别；体内 `try`/`catch` 按字面量自己的返回类型检查 |
| `defer` / `errdefer` / `err` | 体被当普通函数编译，清理链照旧；`defer`/`catch` 体里引用的外层名字也进捕获集合 |
| `@T` / `new` / 切片 / 元组 | 捕获 `@T`（含 `shared @T`） +1，`weak`/`own` 不行；切片只拷描述符；含托管字段的结构体不能捕获（C94）；元组不含托管字段时可捕获 |
| `move` / `own` / `weak` | 闭包变量本身可以 `move`；`move(捕获名)` 不支持（env 字段不是槽），写 `n = NULL`；`own`/`weak` 捕获不支持 |
| GC 环检测 | 闭包签名节点 + 被捕获类型边（§7.2）；环境不进图 |
| `spawn` / `co` / `task` / `thread` | 闭包不能作线程/任务载荷（C96）；`spawn` 的调用目标仍必须是普通函数（C38），闭包调用不是普通函数调用 |
| `async` / `await` | 闭包可以在 async 体内创建/捕获；env 在堆上，挂起/恢复不影响；但闭包不能作为 async 的返回值、参数或 `await` 载荷（跨任务，C96） |
| 方法 / 接口 | 方法可以返回/接收闭包；闭包本身不实现接口；接收者 `self` 不能捕获（C94） |
| `module` / `import` | 签名哈希跨 TU 稳定；字面量函数/环境/跳板都是 `static` |
| `-emit-header` | 发射 `__c6_closure_<hash>` 结构体；env 写成 `void *`（§8.3） |
| C99 后端 | 闭包结构体 + 普通调用 + 普通跳板（§8.2） |
| `test` / `-test` | 可以用闭包；TU 末尾排空队列 |
| f-string | 闭包是托管指针，按 `p`/`x` 输出地址（和其它指针一致）；不特殊处理 |
| `#macro` / `__local` | 宏展开后的 token 参与扫描；卫生重命名对捕获名同样生效 |
| `static inline` | 体内可以写字面量；inline 体在 TU 末尾编译时字面量再入队 |
| 元组解构 / `foreach` | 绑定是普通局部量，可以捕获 |

---

## 11. 明确不做（V2.0）与后续

**V2.0 不做**：

* 捕获可变**引用**（改外层变量）：要「格子」语义，和栈变量模型冲突；
  体里的名字是副本，改副本可以，改外层不行；
* 显式捕获列表 `[x, &y]`：`[` 已经属于下标/切片；
* `shared`/`weak`/`own` 闭包、跨线程/任务闭包；
* 闭包 → C 函数指针（语言级禁止）；
* 闭包的结构相等/哈希；
* 运行期环回收（静态 C66 已经挡住）；
* 静态初始化式里的非空闭包（C95）；
* 捕获数组/VLA/`this`/块作用域 `static`/枚举常量/类型；
* 字面量 `async`（V1 的 C83 不变：字面量本身不能是 `async`）。

**后续可做**（不阻塞 V2.0）：

* `shared closure`：需要把分配点的原子标志从上下文传进来，并检查捕获值都是
  跨线程类型；
* `mut`/`const` 捕获标注：默认副本可写，若用户想声明只读或共享状态，可以
  再加语法；
* 静态初始化：用 `__attribute__((constructor))` 给文件作用域闭包做运行期
  初始化（GC 全局根已经有一套构造/析构）；
* 显式捕获列表（若隐式扫描在极端代码上出现不可接受的编译期开销）；
* 环回收（与 gc-design 的 `c6_graph` 一起）。

---

## 12. 备选方案与否决理由

1. **按值胖指针 `{code, env}`（早期草图）**：`env` 是 `@` 字段，整个结构体
   就是「含托管字段的结构体」，C68 禁止按值拷贝/传参/返回，闭包紧接着就不
   可用了；若为它开特例，每个拷贝点都要生成 +1/−1 钩子，等于重写一遍 `@T`。
   否决，改为「一个机器字的托管引用」。
2. **env 内联进闭包对象（一次分配）**：闭包对象的静态类型是签名级的、被所有
   同签名字面量共享，而每个字面量的捕获字段不同；内联就没有统一的静态类型，
   描述符/字段访问/环边都失去依据。否决，两个对象。
3. **每个字面量一个闭包类型**：`closure int(int)` 变量无法接住第二个字面量，
   参数/返回/跨 TU 全部要转换；类型系统会变得不可用。否决，签名级规范类型。
4. **纯 token 扫描求捕获集合**：分不清遮蔽、`for`/`switch` 作用域、嵌套字面量
   参数、`typedef`/tag，少捕获会静默错编译。否决，用解析器扫描。
5. **在 TU 末尾才分析捕获**：env 布局在字面量处就要定，来不及。否决。
6. **按引用捕获 + 格子（cell）**：局部量在闭包逃逸后已经消失；V1 拒绝捕获
   的理由同样成立。否决。
7. **闭包用 `malloc` + 手写 `drop`**：丢掉 `@T` 的借用规则、跨帧规则、全局根、
   环检测；还得再发明一套生命周期。否决，复用 GC。
8. **闭包 → 函数指针的「胖跳板」**：需要一个全局表把 env 和代码绑定，等于
   运行期闭包注册表；C 函数指针只有一个机器字，无法携带。语言级禁止，C92 的
   包装方向（指针 → 闭包）才是安全的。
9. **`shared` 闭包在 V2.0 一起做**：原子标志必须在分配点确定，而分配点不知道
   目标类型；需要「期望类型」传播到字面量，工作量和风险都不小。推迟。
10. **用 `func` 而不是 `closure` 作关键字**：`func` 更短，但和「函数字面量」
    的语法头 `RET (…)` 组合起来可读性差；`closure` 直接对应文档/术语，且是
    软关键字，不占用用户标识符。

---

## 13. 验收与测试

实现完成后至少要有下面这些用例（文件名按现有约定）：

**正常路径（`tests/closure.c6`，test 块）**

1. 捕获标量、参数、`const`；快照语义（外层改值不影响闭包）；
2. 体里改副本，多次调用看到状态；外层变量不变；
3. 捕获 `@T`：返回闭包后对象仍活着（`__c6_gc_live()`），闭包置 `NULL` 后归零；
4. 闭包返回/作实参/作结构体字段/数组元素；赋值 +1、`NULL` 释放；
5. 闭包参数、闭包返回值、嵌套闭包（内层捕获外层捕获名和外层局部）；
6. 非捕获字面量仍是裸函数指针；把它放进闭包槽（包装）后调用；
7. 函数指针变量 → 闭包；`(closure S)fp` 显式转换；
8. 变参捕获字面量在体里用 `va_arg`；
9. `!error` 闭包 + `try`/`catch`；
10. 体内 `defer`/`err`/`try`/`catch` 引用捕获；
11. struct 返回的闭包；
12. 切片捕获：描述符拷贝、元素修改可见、元素寿命由调用者保证；
13. `sizeof(closure)`、`f == NULL`、`if (!f)`、`move(f)`；
14. 文件作用域 `closure S g = NULL;` 运行时赋值；进程退出释放；
15. 遮蔽：体内同名局部/`for` 初始化/嵌套字面量参数遮蔽外层捕获；外层局部
    遮蔽同名全局量（扫描盲区用例）；
16. `sizeof(捕获名)` / `typeof(捕获名)`（TYPE 条目）；`sizeof(外层数组)` 走
    `C6_CAP_ZERO` 的假左值；`sizeof(int[n])` 把 `n` 捕获成 VALUE；
17. 捕获名是真正的左值：`n++`、`n = …`、`&n`、`p.x = …` 都改 env 字段，
    多次调用状态保留；`move(n)` 给出明确错误；
18. `shared @T` 捕获（原子 +1）；`drop` 结构体/柔性数组/含 `@T` 元组/
    thread/task 句柄捕获报 C94。

**诊断（`tests/errors/closure*.c6`）**

19. C90：`closure` 缺完整签名；匿名类型签名；块作用域 tag 签名（`closure`
    类型和普通字面量签名都测）；`@closure`；
20. C91：闭包 → 函数指针；闭包 → `void *`；不同签名闭包互转；
21. C92：变参函数指针 → 闭包；
22. C93：捕获字面量赋给函数指针；
23. C94：类型/接收者/`static`/枚举常量/数组/VLA/`own`/`weak`/含托管字段结构体/类型含块内 tag 的值/`drop` 结构体/柔性数组/含 `@T` 元组/thread 句柄；`sizeof(VLA)`；
24. C95：文件作用域/`static` 局部的非空闭包初始化；
25. C96：`shared`/`weak`/`own` 闭包；闭包进 `spawn`/`async`；
26. C97：`f->x`、`*f`、`f + 1`、`new closure …`；
27. C98：自捕获；
28. C66：`struct Node { closure int() f; }` + 捕获 `@Node` 的环。

**ABI / 跨 TU / 头文件**

29. `tests/abi/closure.c6` + `_plain.c`：c6 TU 创建闭包、纯 C TU 只按
    `struct __c6_closure_h *` 传递/保存（不调用）；退出码一致；
30. `tests/tu/closure.c6`：闭包类型作函数参数/返回值跨 TU；
31. `-emit-header`：闭包结构体、typedef、struct 返回、切片参数按顺序写出，
    结构体被 `#pragma pack` 钉住，生成的 C 头能被 `gcc` 单独编译。

**C99 后端**

32. `tests/c99/closure.c6`：捕获、包装、跳板、`!error`、struct 返回、
    变参字面量、`__c6_gc_live()` 归零；`gcc -std=c99 -pedantic -Wall
    -Wextra -Werror` 零告警。

**零影响与全量回归**

33. `make test-plain`（关掉 `C6_EXT`）：闭包代码全部在 `#ifdef C6_EXT` 内；
34. `make selftest`、`BACKEND=c99 tests/run-tests.sh`、`make selfhost`、
    `make c99selfhost` 全绿；
35. 现有 `tests/fn_lit.c6`、`tests/errors/fn_lit.c6`、`tests/abi/fn_lit*`
    一行不改仍通过（V1 兼容是硬约束）。

---

## 14. 实施顺序

见 [closure-todo.md](closure-todo.md)。阶段划分：

| 阶段 | 内容 |
|---|---|
| P0 | `closure` 类型：软关键字、签名表、布局、打印、typedef/sizeof、诊断 C90 |
| P1 | 捕获扫描 + env 布局 + 标量捕获 + 调用改写（原生后端） |
| P2 | 托管捕获：描述符、+1/−1、GC 归零、环边 C66、`@T` 测试 |
| P3 | 嵌套字面量、TYPE 捕获、`defer`/`err`/`try`/`catch`/变参/`!error`/struct 返回 |
| P4 | 包装与跳板、`-emit-header`、跨 TU、C99 后端 |
| P5 | 诊断补齐、文档、全量回归/自举 |
