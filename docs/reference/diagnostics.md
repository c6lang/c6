# 诊断码参考（C1–C134）

> **状态**：现行规范 · **索引**：[文档索引](../README.md) · **语法**：[grammar.md](grammar.md) · **设计**：[plan.md](../design/plan.md) · **实现状态**：[status.md](../history/status.md)
>
> 这一篇是编译器静态约束与诊断文本的唯一清单：左边是约束编号，中间是编译器检查什么，
> 右边是违反时打印的消息。编号在 `grammar.md` 与代码注释里被引用，**不要重排**。
> 错误码号段（`errbase.def`、C124–C128）的语义见 [grammar.md §8.1](grammar.md#81-错误码号段与-errbasedef)。

下列约束由编译器检查，违反时给出对应的诊断信息。

| 编号 | 约束 | 诊断 |
|---|---|---|
| C1 | `try` 只能出现在返回类型标记了 `!error` 的函数体内 | `'try' requires the enclosing function to return 'T!error'` |
| C2 | `try` 的操作数必须是调用表达式 | `'try' requires a call to a function returning 'T!error'` |
| C3 | `try` 的被调函数返回类型必须标记了 `!error` | 同 C2 |
| C4 | `catch` 的操作数必须是调用表达式 | `'catch' requires a call to a function returning 'T!error'` |
| C5 | `catch` 的被调函数返回类型必须标记了 `!error` | 同 C4 |
| C6 | `catch` 的处理块必须自己离开（`return` / `return err(id, msg)` / `goto`） | `'catch' body must end with 'return', 'err()' or 'goto'`；静态判断不出来时改为插入运行时陷阱 |
| C7 | `catch` 必须带操作数 | `'catch' has no operand` |
| C8 | `!error` 必须修饰函数（或函数指针）声明符 | `'!error' requires a function declarator` |
| C9 | `!error` 的返回类型必须是 C 允许按值返回的类型（数组不行）：整数类标量失败时返回 `-1`，聚合与 `float`/`double`/`long double` 失败时清零 | `'!error' does not support a 'int [3]' return type` |
| C10 | `expr` 中不能直接写裸 `catch` | 同 C7 |
| C11 | `try` / `catch` / `err` 不得出现在 `defer` / `errdefer` 体内 | `'try' is not allowed in a defer body` 等 |
| C12 | 返回类型大小未知（不完整类型）的函数不能传播错误 | `cannot propagate an error from a function with an incomplete return type` |
| C13 | 模块文件里的文件作用域符号被改写为 `NAME__sym`，`main` 除外；点号名逐段拼成 `a__b__` | — |
| C14 | `mod.member` 只在 `mod` 已 `import`（或就是当前模块）时成立 | `'mod' is neither an enum nor an imported module (use 'import mod;')` |
| C15 | `T!error` 调用的结果必须被 `try` 或 `catch` 消费（`sizeof` / `typeof` 的操作数不产生调用，不受此限） | `the result of a 'T!error' function must be used with 'try' or 'catch'` |
| C16 | `err` 必须带 `id` 与 `msg` 两个实参（没有重抛形式） | `'err' requires an error id and a message` |
| C17 | `err` 只能出现在返回 `T!error` 的函数里 | `'err' requires the enclosing function to return 'T!error'` |
| C18 | `error` 只能作为返回值，不能作为形参（含 `error*` / `error[]`，K&R 形参声明也算） | `'error' can only be a return type, not a parameter` |
| C19 | 每个变量定义都必须带初始化器（`extern` 声明、形参、成员、`typedef`、`static` 变量除外；VLA 豁免） | `variable 'x' must be initialized` |
| C20 | 方法调用要求：接收者是指向结构体/联合体的指针（`p->f()`），或者是可作左值的结构体/联合体值（`v.f()` 按 `&v` 传）；该类型有同名方法、没有同名字段、方法在调用点之前已声明，且方法后面必须紧跟 `(` | `method 'f' must be called`；`duplicate method 'f' for 'T'`；`lvalue expected`（值接收者不是左值）；其它情况按普通成员访问报 `field not found: f` |
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
| C34 | `co` / `spawn` / `task` 只能出现在函数体内 | `'co' can only be used inside a function`；`'spawn' can only be used inside a function`；`'task' can only be used inside a function` |
| C35 | `co` 的操作数必须是一次 `async` 调用 | `the operand of 'co' must be the handle of an 'async' call`；`'co' requires an 'async' function: 'f' is a plain function (use 'spawn')` |
| C36 | `task` 的初始化器必须是一次 `async` 调用（类型从初始化器推出，所以不能省） | `the initializer of a 'task' variable must be a call of an 'async' function`；`'=' expected` |
| C37 | `async` 函数不能是方法（方法调用会被改写成普通调用，指向不存在的函数体） | `'async' functions cannot be methods: '…'` |
| C38 | `spawn` 的操作数必须是**普通函数**的调用：不能是 `async` 函数（用 `co`）、不能是函数指针、参数类型必须能写进参数块（数组/VLA/函数指针形参不行） | `the operand of 'spawn' must be a call of a plain function`；`'spawn' requires a plain function: 'f' is 'async' (use 'co')`；`'spawn' does not support this signature: 'f'` |
| C39 | `spawn` 不支持变参函数（参数块要按声明的形参布局） | `'spawn' does not support variadic functions: 'f'` |
| C40 | `spawn` 不支持 `!error` 函数（错误没人接） | `'spawn' does not support fallible functions: 'f'` |
| C41 | 裸 tag 只有在该 tag 可见、且同名**普通符号**（变量/函数/枚举常量/typedef）不在作用域内时才当作类型名；只接受已声明的 tag，不隐式前置声明；语句开头的 `TAG:` 仍是标签；K&R 定义里形参名与 tag 同名时按原型解析 | —（遮蔽/未声明时按普通 C 解析：`';' expected`、`'X' undeclared`、`initialization of incomplete type`） |
| C42 | `foreach` 只能出现在函数体内 | `'foreach' can only be used inside a function` |
| C45 | 区间只产生下标：头里只能有一个绑定 | `a range yields only an index: write 'foreach (size_t i = lo:hi) { ... }'` |
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
| C67 | `-b`（边界检查）不能与 `async` 同用；不能把帧交给 body 的目标（c67）编译期拒绝 `async` | `'-b' cannot be combined with 'async'`；`'async' is not supported on this target` |
| C68 | 含托管字段的结构体不能按值拷贝/传参/返回 | `'%s' holds managed references: %s is not possible. Allocate it with 'new' and hold it in '@%s'` |
| C69 | 托管引用只能存进托管槽；借出的指针不能保存 | `a new reference cannot be stored in '%s': it is not a managed reference`；`storing a borrowed reference keeps it alive without counting` |
| C70 | 形参是借用，不能改写 | `'%s' is a borrow: it owns nothing, so it cannot be reassigned (copy it into an owned local first)` |
| C71 | `own @T` 只接受 `new` 的结果、`move()` 或空值 | `'%s' takes ownership: only a fresh reference ('new') or the empty value can be stored in it` |
| C72 | `shared @T` 槽要求对象用 `new shared T` 建（计数在对象头里） | `a 'shared' slot needs an object with an atomic count: create it with 'new shared T', not 'new T'` |
| C73 | `@T`/`own @T` 不能跨线程或跨帧（`spawn`/`async` 只收 `shared @T`） | `passing a reference to 'spawn' requires 'shared'`；`passing a reference to 'async' requires 'shared'` |
| C74 | `union` 不能含托管字段；含托管字段的结构体必须在文件作用域定义、不能有托管柔性数组 | `'%s' is a union with a managed field: ...`；`... is defined in a block: define it at file scope`；`... has a flexible array of managed references` |
| C75 | `memcpy`/`memmove` 不能作用于含托管字段的对象；`memset` 会漏引用（告警） | `... breaks reference counting; use a typed copy or 'own'` |
| C76 | 析构 `drop` 不能手动调用、不能失败/挂起、必须返回 `void`、每个类型至多一个 | `a destructor cannot be called: ...`；`a destructor cannot fail: ...`；`a destructor cannot suspend: ...`；`'%s' already has a destructor` |
| C77 | `move(x)` 的操作数必须是槽里的托管引用 | `'move' needs a managed reference held in a slot, not a '%s'` |
| C78 | `thread` 句柄的初始化式必须是 `spawn` 调用；句柄不能跨线程传、不能手动 `free` | `the initializer of a 'thread' variable must be 'spawn f(...)'` |
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
| C94 | 捕获了不能按值拷贝的名字/类型：`own`/`weak` 引用、块作用域 `static`/函数/枚举常量/类型/tag/枚举、数组、VLA、方法接收者、含 `drop` 或托管字段的结构体、含托管元素的元组、柔性数组、thread/task 句柄 | `a closure cannot capture '%s': an 'own' reference cannot be copied`；`a closure cannot capture '%s': upgrade the 'weak' reference to '@T' first`；`a closure cannot capture the block-scope 'static' '%s': move it to file scope`；`a closure cannot capture the function '%s' declared in a block: move the declaration to file scope`；`a closure cannot capture the constant '%s': move it to file scope`；`a closure cannot capture the array '%s': pass a slice 'T[:]' or a pointer instead`；`a closure cannot capture the variable length array '%s': its size is only known at run time`；`a closure cannot capture '%s': its type is declared in a block (move the type to file scope)`；`a closure cannot capture the method receiver '%s': ...`；`a closure cannot capture '%s': the type has a 'drop' destructor, ...`；`a closure cannot capture '%s': it holds managed references ...`；`a closure cannot capture '%s': a thread/task handle owns a resource ...`（分条消息全文见 [closure-design.md](../design/closure-design.md) §9） |
| C95 | 文件作用域或 `static` 局部的初始化式里不能放非空闭包（要分配）；`NULL` 可以，运行时再赋值 | `a closure is not a constant expression: initialize it at run time ('NULL' is fine at file scope)` |
| C96 | 闭包不能带 `shared`/`weak`/`own`，也不能跨线程或任务（`spawn`/`async`/`thread` 的载荷、`async` 的参数与返回值） | `a closure cannot be 'shared', 'weak' or 'own': V2 closures are confined to the thread that creates them`；`a closure cannot cross a thread or a task: V2 closures are confined to the thread that creates them` |
| C97 | 对闭包做不透明操作：`->`、`*`、下标、算术、`new`、`offsetof`（只能调用、和 `NULL` 比较、拷贝、`move`、置空释放） | `'%s' is a closure: it can only be called, compared with NULL, copied, moved or released`；`a closure cannot be created with 'new': use a function literal or a function pointer` |
| C98 | 直接赋给自己的字面量捕获了目标槽自己（闭包会看到旧值；递归要写成具名函数） | `'%s' is captured by the closure being assigned to it: the closure would see the old value (use a named function for recursion)` |
| C99 | 迭代器方法签名：`has()` 返回整数、`next()` 返回完整可赋值对象类型（`void *` 会擦除元素类型）、两个方法只接收 receiver | `iterator method 'has' must return an integer type (got 'T')`；`iterator method 'next' must return a value (got 'void')`；`iterator method 'next' must return an object type (got 'T')`；`iterator method 'next' cannot return 'void *': the element type would be erased (return 'E' or 'E *' with a concrete E)`；`iterator method 'has' must take only the receiver`；`iterator method 'next' must take only the receiver`；`iterator method 'next' cannot return a borrowed managed reference 'T'` |
| C100 | 迭代器只产生一个值绑定（`foreach (T v = it)`） | `an iterator yields only a value: write 'foreach (T v = it) { ... }'` |
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
| C120 | `Tag.成员` 里 `Tag` 必须是可见的枚举，成员必须存在 | `'Fruit' has no member 'APPLE'`；`'Nope' is neither an enum nor an imported module (use 'import Nope;')` |
| C123 | 体内成员函数里裸名的解析次序：局部/形参 > 实例的字段与方法 > 文件作用域其他符号（文件作用域的同名函数不抢字段）；实例化不得把模板方法的原名或形参名留在调用方作用域里 | —（曾表现为 `redeclaration of 'i'` / 把字段当成函数） |
| C122 | 模块名就是它的路径：`import a.b` 只按 `a/b.c6`（或 `a/b.c`）查找，扁平拼写被拒绝，找不到文件也是错误 | `module 'a.b' must live at 'a/b.c6', not in a flat 'a.b.c6': a module's name is its path`；`cannot find module 'a.b': expected a file at 'a/b.c6' in the search path (a module's name is its path)` |
| C124 | `err(..)` 只能写成 `return err(id, msg)`：单独作表达式、语句、或赋值右值都是错误；`error` 不是一等类型，不能声明对象、形参、字段 | `'err' can only be used as 'return err(id, msg)'`；`'error' is the type of 'err()': it can only be returned from a 'T!error' function` |
| C125 | `enum Error` 的成员必须落在 `0..4095`（`0` 仍是「没有错误」，不参与加基址） | `error code of '%s' is not in 0..4095` |
| C126 | `errbase.def` 的每一行是 `<模块名> <slot>`；slot 只能落在钉号池 `0x6100..0x7FFF`；std 模块的号段内置在编译器里，写进来是错误；同一个号不能给两个模块，同一个模块不能钉两个号 | `%s:%d: module '%s' needs a slot`；`%s:%d: '%s' is not a slot number`；`%s:%d: '%s' is not a module name`；`%s:%d: the blocks of the std modules are built into the compiler: remove '%s' from errbase.def`；`%s:%d: slot 0x%lx is outside the pinned pool 0x6100..0x7fff`；`%s:%d: module '%s' is pinned to 0x%04x already`；`%s:%d: slot 0x%04x is used by '%s' already` |
| C127 | 一次编译里两个模块不能落在同一个号段（自动池的哈希撞了，或者表里钉重了）；撞了就报错，不静默 | `tcc: error: error ids: modules '%s' and '%s' share the block 0x%04x; pin one of them in errbase.def` |
| C128 | `-errbase-file=FILE` 指定的表必须存在（只有默认的 `errbase.def` 会被自动创建）；钉号池用完也是错误 | `cannot read error block table '%s'`；`tcc: error: the pinned pool 0x6100..0x7fff is full: errbase.def cannot grow` |
| C121 | 声明式头里的绑定：不能是 `void`/数组/函数，不能改变托管所有权，下标必须是整数，头里必须写出类型和名字 | `a 'foreach' binding cannot be 'void'`；`a 'foreach' binding cannot be an array`；`a 'foreach' binding cannot be a function`；`a 'foreach' binding cannot change ownership: 'v' holds a managed reference`；`the index of a 'foreach' must have an integer type, not 'double'`；`a type name expected in the head of 'foreach'`；`'foreach' needs a parenthesised head now: write 'foreach ((size_t i, T v) = a) { ... }'` |
| C129 | 宏定义的角色词后面必须有名字，名字后面必须有 `(`，体必须用 `{` 包住 | `a macro name must follow the '%s' role`；`'(' expected after the macro name '%s'`；`'{' expected: the body of macro '%s'` |
| C130 | 宏的每个形参都必须带角色，角色后面必须有名字 | `every macro parameter needs a role: 'expr', 'stmt', 'ident' or 'type'`；`a parameter name must follow the '%s' role`；`',' or ')' expected in a macro parameter list` |
| C131 | `#` / `##` 的操作数必须是 `ident` 角色的形参 | `'%s' has role '%s': '#' and '##' need an 'ident' parameter` |
| C132 | `expr` 宏的体必须是一个表达式（结尾允许一个 `;`） | `an 'expr' macro body is one expression: use a 'stmt' macro and '({ ... })' for statements`；`an 'expr' macro needs an expression in its body` |
| C133 | `##` 不能落在宏体的两端；`__local` 后面必须是标识符，且不能被 `#` / `##` 碰 | `'##' cannot appear at either end of a macro body`；`'__local' must be followed by an identifier`；`cannot apply '#' or '##' to a '__local' name` |
| C134 | `#macro` 指令已删除，宏改写成文件作用域的声明形式 | `'#macro' is gone: write 'stmt name(expr a, ident n) { ... }' instead (the leading role says how the expansion is used)` |

（闭包的 C90–C98 见 [closure-design.md](../design/closure-design.md) §9，消息与
`tccgen.c` 同步落地；C80 在 V2 下只剩「没有按值捕获、却引用了块作用域名字」
这一种情况。
枚举成员限定的 C117–C120 见 §3.8。
泛型的 C103–C116 已全部落地：C106 与 C104 共用一条消息（多参数必须加括号），
C109 由普通类型检查给出（`cannot convert 'struct Vect_char' to 'struct Vect_int'`），
见 [generics-design.md](../design/generics-design.md) §9。）
