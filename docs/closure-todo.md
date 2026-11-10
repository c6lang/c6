# c6 捕获式闭包（V2）实施 todo

设计见 [closure-design.md](closure-design.md)。本文是执行清单：阶段、改动点、
测试、验收命令。**设计已定稿，代码尚未开始**；每个阶段独立可编译、可回归、
可提交。

约定：

- 构建：`make build`（仓库根；`tcc/` 已配置过，必要时先 `make bootstrap`）。
- 每阶段验收（全绿才算完成）：

  ```sh
  make build
  make selftest                       # tests/run-tests.sh，原生后端
  make test-plain                     # 关掉 C6_EXT 的零影响回归
  ```

  P4 起再加：

  ```sh
  BACKEND=c99 tests/run-tests.sh      # C99 输出后端
  make c99test
  ```

- 新测试位置按现有约定：`tests/*.c`（`test "…" {}`）、
  `tests/errors/*.c`（`// EXPECT <片段>: error: <文本>`）、
  `tests/exit/*.c`（`// EXIT n`）、`tests/abi/*.c` + `_plain.c`、
  `tests/c99/*.c`、`tests/tu/<name>.c`。
- 提交：一个阶段一个提交，标题 `closure: Pn <一句话>`，正文写验收命令与结果。
- 全程红线：**V1 的语法、类型、ABI、C80–C83 不变**。现有
  `tests/fn_lit.c`、`tests/errors/fn_lit.c`、`tests/abi/fn_lit*` 一行不改。

---

## 阶段总览

| 阶段 | 内容 | 依赖 | 主要风险 |
|---|---|---|---|
| P0 | `closure` 类型（无字面量） | — | `parse_btype` 软关键字、签名哈希稳定性 |
| P1 | 捕获扫描 + env + 标量捕获 + 调用改写 | P0 | 扫描 pass 的副作用隔离 |
| P2 | 托管捕获、描述符、释放、环边 | P1 | env/闭包描述符的字段偏移 |
| P3 | 嵌套字面量、TYPE 捕获、全部控制流/签名特性 | P1/P2 | 嵌套扫描的 base 语义 |
| P4 | 函数指针包装、跳板、头文件、跨 TU、C99 | P1 | 变参限制、C99 声明顺序 |
| P5 | 诊断补齐、文档、全量回归/自举 | 全部 | 旧用例零回归 |

---

## P0 `closure` 类型

目标：`closure int(int)` 能被解析、打印、`sizeof`、typedef、作形参/返回值/
字段/数组元素；还没有捕获字面量（捕获仍按 V1 报 C80）。

改动点：

- `tcc/tcc.h`
  - `struct SymAttr` 用掉两个空闲位：`c6_closure : 1`、`c6_receiver : 1`；
  - `Sym` 增加 `int c6_sym_seq;`（P1 用，P0 先加好并在 `sym_push2()` 里发号）；
    增加 `unsigned char c6_cap_kind;`（0 = 普通符号；P1 用；别名偏移仍用
    `Sym.c`，不另加字段）；
  - `TCCState` 增加 `C6Closure` 动态数组（或静态链表，跟随
    `c6_slices`/`c6_tuples` 的现有做法），`c6_tu_reset()` 里清空。
- `tcc/tccgen.c`
  - `C6Closure`：

    ```c
    typedef struct C6Closure {
        int tag;              /* struct __c6_closure_<hash> */
        unsigned long long hash;
        CType ftype;          /* 用户签名：RET (P...) [!error] */
        CType code_type;      /* RET (*)(void *, P...) [!error] */
        char *sig_text;       /* 规范化签名文本，哈希碰撞时比较 */
        int variadic, fallible;
        struct C6Closure *next;
    } C6Closure;
    ```

  - `c6_closure_get(CType *ftype)`：查表 → 没有就
    1. 检查签名稳定性（C90）：匿名/块作用域 tag 要报 C90；参数里的数组/VLA/
       函数类型先按 `convert_parameter_type()` 归一（数组/VLA→指针、函数→
       函数指针，这是 C 的函数类型兼容规则，不是错误）；返回类型/成员里的
       VLA 本来就不是合法签名；函数调用约定（`func_call`）也要进签名文本，
       否则 ABI 不同的函数指针会共享闭包类型；
    2. 用 `c6_closure_sig_text()` + FNV-1a 64 算 `hash`；
    3. 先确保全局的 `struct __c6_env_base { char __c6_unused; }` 存在，并把它
       的 `type.gq` 置成 `C6_GQ_MANAGED`；在全局作用域建
       `struct __c6_closure_<hash> { code; @__c6_env_base *env; }`，
       **手工定布局**：code 0、env `PTR_SIZE`、size `2*PTR_SIZE`、
       align `PTR_SIZE`（不要走 `struct_layout()`，`#pragma pack` 不能改它）；
    4. `find_field` 用 `__c6_code` / `__c6_env` 两个内部名；`__c6_env` 字段
       类型是 `@__c6_env_base *`（托管），`__c6_code` 不是托管字段；
    5. 登记到表里，`type.ref->a.c6_closure = 1`，并把
       `type.ref->type.gq = C6_GQ_MANAGED`（`c6_is_managed()` 的可信副本）；
    6. 合成类型是在函数体里按需创建的，`c6_gc_collect()` 的
       「含托管字段的结构体不能定义在块里」（`local_scope != 0`）必须放行：
       用 `c6_gc_synthetic` 标志，或临时 `local_scope = 0` 再恢复。
  - `c6_closure_of_type(CType *t)`：`VT_PTR` + `t->ref` + `ref->a.c6_closure`。
  - `parse_btype()`：在识别 typedef 之前按名字识别 `closure`（用
    `sym_find(tok)` 判遮蔽：**变量或 typedef 都算**；`c6_soft_kw_shadowed()`
    只认 typedef，不能单独用。**不要**加进 `c6_soft_kw()` 的语句分派，
    否则 `block()` 的默认分支会把它当 `c6_task_decl()`）；进入
    `c6_closure_btype()`：
    * `next()`；递归 `parse_btype()` 读返回类型；
    * `c6_parse_fallible_suffix()`；
    * `type_decl(..., TYPE_DIRECT | TYPE_ABSTRACT)` 读 `(参数…)`；
    * 必须是 `VT_FUNC` 且没有名字；`()` 当无参；
    * `c6_closure_get()` 得到托管指针类型，`*type = result`。
  - `c6_lit_type_start()` 加 `closure`（返回类型本身是闭包的字面量）。
  - `type_to_str()`：打印 `closure RET (P...)`（含 `!error`、变参、嵌套）。
  - `c6_gc_new_expr()`：解析出闭包类型后报 C97（闭包只能由字面量或函数指针
    包装产生，不能 `new`）。
  - `shared`/`weak`/`own` 后跟 `closure` 时吞掉限定符并报 C96；`parse_btype()`
    的 qualifier 分支要把 `closure` 当作类型开头，否则会退化成语法错误。
  - `is_compatible_types()` 不需要特判：规范表保证同签名同 tag。
  - `sym_find(tok)` 用来判遮蔽（变量/typedef 都算）；`c6_soft_kw()` 不加
    `closure`（避免 `block()` 的默认分支把它派给 `c6_task_decl()`）。
- `tcc/tccpp.c`：不用改（软关键字不进 `tcctok.h`）。
- 注释/文档：`closure-design.md` §1–§3；`grammar.md` 只加「V2 设计预览」指针。

测试：

- `tests/typename.c`：`closure int(int)`、`closure void(int[:], int)`、
  `closure int!error(int)`、`closure int(int) *`、数组/typedef 的类型字符串。
- `tests/closure.c` 先放：`closure int(int) f = NULL; if (f) …;`、
  `sizeof(closure int(int)) == sizeof(void *)`、struct 字段/数组、typedef。
- `tests/errors/closure_type.c`：C90 的缺签名、匿名 tag、块作用域 tag、
  `@closure`；C96 的 `shared`/`weak`/`own closure`；C97 的 `new closure …`。

验收：`make build && make selftest && make test-plain`。

风险与对策：

- `parse_btype` 的递归：`closure closure int(int)(int)`（返回闭包的闭包）
  要能工作；用 `c6_lit_lead()` 的投机做冒烟。
- 签名哈希的稳定性：先写一个 `type_to_str` 风格的结构化序列化，单测两个
  等价写法（参数顶层 `const`、数组 vs 指针）得到同一个 hash。

---

## P1 捕获扫描 + 环境 + 标量捕获 + 调用改写

目标：`int n; closure int() f = int() { return n; }; f()` 在原生后端跑通；
env 只放标量/指针/无托管结构体。托管捕获留到 P2，但扫描/布局接口先按最终
形状实现。

改动点：

- `tcc/tccgen.c` 的 `C6Lit`：

  ```c
  typedef struct C6Cap {
      int tok;           /* 源名字（诊断/别名用） */
      CType type;        /* 捕获类型 */
      int off;           /* env 字段偏移（TYPE 条目为 0） */
      unsigned char kind;/* C6_CAP_VALUE / C6_CAP_TYPE / C6_CAP_ZERO */
  } C6Cap;

  struct C6Lit {
      … V1 字段保留 …
      C6Cap *caps; int ncaps;
      int capturing;      /* 只有 C6_CAP_VALUE 才置位 */
      CType ftype_code;   /* 带 env 的代码签名（用户签名仍是 V1 的 ftype） */
      CType env_type;
      CType closure_type; /* @struct __c6_closure_<h> */
      int env_desc;       /* 描述符 token（P2 用） */
  };
  ```

  `c6_tu_reset()` 里随 `C6Lit` 一起释放 `caps`（V1 的 `caps` 是 `int*`，
  V2 变成 `C6Cap*`）。

- **核心小改动**：`sym_push()` 的重复定义检查放行
  `s->prev_tok->c6_cap_kind != 0`。捕获别名是编译器生成的，同名参数/局部要能遮蔽
  它；参数和局部之间的重复定义检查不受影响。

- 捕获扫描 `c6_lit_scan(C6Lit *lit)`（设计 §5）：
  - 在 `c6_lit_parse()` 里 `skip_or_save_block(&body)` 之后调用；
  - 先对**所有**字面量（不只 closure 类型）检查签名全局稳定：形参/返回类型
    里的块作用域 tag 在 TU 末尾不可见（C90 第三条消息）；
  - `begin_macro(body, 1)`；保存/恢复设计 §5.4 的全部状态；
  - `c6_lit_scanning++`；`nocode_wanted = 1`；压哑符号 + 参数；
    `scan_base = ++c6_sym_seq`；设置 `func_vt`/`c6_func_sym` 等；
  - `block(0)`；弹符号；恢复；
  - 捕获记录入口：值解析/typedef/tag 解析三处（沿用 V1 的
    `c6_lit_cap_check` 挂点，改成 `c6_lit_cap_note()`）。
  - 分类规则见设计 §4.2；不能捕获的报 C94。分类顺序：先 `s->c6_cap_kind != 0`
    （外层别名，递归记录），再 `sym_scope == 0`（文件作用域），再
    `c6_sym_seq >= scan_base`（体内），最后才是捕获候选。
  - `c6_scan_uneval > 0` 时：VLA 类型/`sizeof(VLA)` 直接 C94；否则记
    `C6_CAP_ZERO`。VLA 维度表达式解析时临时把 `c6_scan_uneval` 清零，维度里
    的名字记 `C6_CAP_VALUE`。
  - 捕获类型必须全局稳定：`c6_type_global()` 递归检查指针/数组/函数/
    切片/元组/闭包里的 struct/union/enum tag 的 `sym_scope == 0`（含匿名的
    块内 tag）；函数参数先 `convert_parameter_type()` 归一；不满足报 C94。
- 抑制表（`c6_lit_scanning` 检查）：`c6_defer_parse`、`c6_defer_begin`、
  `c6_gc_site`/`c6_gc_root_add`/`c6_gc_arg_*`、`c6_sp_parse`、
  `c6_method_register`、`c6_lit_parse` 的**嵌套分支**（不能整个跳过：照常
  解析头/体、递归扫体，只是不入队、不发布符号）、`c6_auto_impl_ensure`。
  `nocode_wanted=1` 已经挡代码/数据；这张表只挡全局登记。切片/元组/闭包类型
  **照常 intern**（都挂 `global_stack`，元素/签名已按全局稳定规则检查），
  不要为扫描另造临时类型：那样真实编译还得再建一份，反而容易不一致。
- 嵌套字面量：扫描模式下只「解析头 + 存体 + 递归扫体 + 丢弃」，不入队、
  不领名字；值类型给 V1 函数指针。
- env 类型：`c6_closure_env_get(lit)` 在全局作用域建
  `struct __c6_env_<n>`，字段 `__c6_c0…`；用 `struct_layout()` 算偏移/大小/
  对齐，偏移存 `caps[i].off`；同样要用合成类型标志绕过 `c6_gc_collect()` 的
  块作用域检查；
  没有托管字段时描述符为 0。
- 字面量处发射（设计 §6.1）：
  - 两个隐藏 `VT_LOCAL` 槽；
  - `__c6_gc_new` 的调用照 `c6_gc_new_expr()` 的写法（size/count/desc/flags）；
  - 捕获初始化：`env + off` → `indir()` → `vstore()`；
  - `cl->code`/`cl->env` 的存储用 `c6_gc_no_hook` 或 FRESH 存储；
  - 结果 `gc_state = C6_GC_FRESH`；内部裸指针运算用 `c6_gc_plain()` 擦标记；
  - 没有 `C6_CAP_VALUE` 时直接走 V1（TYPE/ZERO 别名照压）。
- 代码符号：`ftype_code = c6_func_prepend_env(&lit->ftype)`（匿名 `void *`
  首参）。**扫描后再发布**：`external_sym(lit->name,
  lit->capturing ? &ftype_code : &lit->ftype, VT_STATIC|VT_EXTERN, …)`；
  扫描期间用压在本地的临时 Sym（类型 `ftype_code`）供 `try`/`return` 用，
  扫完弹掉。
- 体发射（设计 §6.3）：
  - `gen_function()` 在压哑符号后、`sym_push_params()` 前调用
    `c6_lit_push_captures(lit)`（在 `c6_lit_replay && lit->ncaps` 时；
    TYPE/ZERO 条目也要压别名，即使 `capturing == 0`）；
  - 别名只负责**名字解析/遮蔽**：三种都 `sym_push(tok, &type, VT_CONST, 0)`，
    `c6_cap_kind = VALUE/ZERO/TYPE`；TYPE 的 `type.t` 加
    `VT_TYPEDEF`。压别名时临时 `local_scope = 1`（函数作用域），并在 `sym_push()` 的
    redeclaration 检查里放行 `prev_tok->c6_cap_kind != 0`：同名参数/局部遮蔽别名，
    参数和局部之间照旧报重复定义；`c6_lit_cap_check` 必须先认 `c6_cap_kind != 0`；
  - 设 `body_base = ++c6_sym_seq`，再压参数；**`sym_copy()` 会连旧
    `c6_sym_seq` 一起拷**，所以 `sym_push_params()` 每复制一个参数要重新
    发号，否则参数会被当成外层捕获；只有 `capturing` 时参数链首是无名 env
    参数，找到并存 `c6_lit_env_sym`（否则不设）；
  - `unary()` 值解析的 cap 分支（设计 §6.3 的伪代码）：VALUE 压
    `c6_lit_env_push()`（`env` 参数，`void *`）+ 字段偏移 + `indir()`，得到
    **动态左值**（写/`++`/`&`/`x.f` 都自然工作）；ZERO 压
    `vset(&type, VT_CONST|VT_LVAL, 0)` 的假左值（只给 sizeof/typeof 做类型
    检查，不出代码）；TYPE 不经过值路径。`c6_lit_scanning` 时不分 kind，
    一律压假左值，**不碰 env**（嵌套扫描没有自己的 env 参数）。
- 调用点（设计 §6.4）：`unary()` 的 `tok == '('` 分支、通用调用代码之前：
  - 若 `c6_closure_of_type(&vtop->type)`：把值存进隐藏槽（借用）、取
    `env` 放进 `c6_self_loc`、`c6_self_pending = 1`、`vtop` 换成
    `tmp->__c6_code`（类型 `ftype_code`），再落回通用调用。
  - `c6_self_type` 用 `void *`；`c6_self_pending` 本来就在实参之前消费。
  - 隐藏槽是借用：`c6_gc_no_hook` 直接存 / 读，不 +1、不 release。
- 非捕获字面量：保持 V1 路径，一个字节不改。
- 表达式值（设计 §6.1.1）：`c6_lit_parse()` 扫描后按 `capturing` 决定
  `vpushsym`（V1）或载入闭包指针（`vtype = closure`、`gc_state = FRESH`）；
  `c6_lit_infer_decl()` 捕获时把变量推断为 `closure` 类型（托管槽，
  **不要再 `mk_pointer`**；vtop 已经是引用），
  空捕获时仍推断函数指针。

测试：

- `tests/closure.c`：
  - 捕获标量/参数/`const`；快照语义；外层改值不影响闭包；
  - 体里改副本，多次调用看到状态；
  - 闭包返回/实参/结构体字段/数组；赋值 +1、`NULL` 释放；
  - 非捕获字面量仍是裸函数指针；`sizeof(closure)`、`f == NULL`；
  - 遮蔽：体内局部、`for` 初始化、嵌套字面量参数、`sizeof(捕获名)`；
  - `__c6_gc_live()` 在闭包释放后归零（标量捕获版）。
- `tests/errors/closure_capture.c`：C93（捕获字面量赋给函数指针）、
  C94 的数组/VLA/类型/`static`/枚举常量、类型含块内 tag 的局部结构体、C98 自捕获。
- `tests/errors/closure_opaque.c`：C97。
- `tests/tu/closure.c`：跨 TU 只传不建（P4 再补包装）。

验收：`make build && make selftest && make test-plain`。

风险与对策：

- **扫描副作用**是最大风险。P1 先只支持标量捕获，用一小步提交把扫描跑通；
  每加一个被抑制的子系统就补一条测试。若某子系统不好抑制，先在扫描模式
  直接报「暂不支持」并记 todo，不要带病前进。
- 扫描的 `nocode_wanted=1` 与已有 `NOEVAL_WANTED` 叠加：保存/恢复要按位处理，
  不要直接赋 0。
- `func_vt`/`c6_func_sym`/`defer_state` 等全局状态必须**成对**保存恢复；
  写一个 `C6ScanSave` 结构体，集中管理（设计 §5.4）。
- 别让扫描给 `c6_lit_count`/`anon_sym` 发号（嵌套字面量不入队）。

---

## P2 托管捕获：描述符、释放、环边

目标：捕获 `@T`/`closure`；返回闭包后对象仍活着；闭包释放后 `live()` 归零；
`struct Node { closure int() f; }` 的环报 C66。

改动点：

- env 描述符：`c6_closure_env_get()` 给 env 结构体登记 `C6GcType`，
  字段 = 直接托管的捕获（`@T`、`closure`），kind `STRONG`；
  含托管字段的结构体/切片不进描述符（前者 C94，后者不拥有元素）。`@` 的被指
  类型用 P0 建好的 `struct __c6_env_base`。
- 闭包描述符：`__c6_env` 字段登记 `STRONG`，`to_tag = __c6_env_base`；
  `__c6_code` 不是托管字段。合成结构体建好后要**显式调用
  `c6_gc_collect()`**（`struct_decl` 的自动收集不会经过合成路径）；
  描述符由 `c6_gc_flush_descs()` 在 TU 末尾发。
- 字面量处的捕获初始化直接走 `vstore()` 的托管钩子：读外层 `@T`（借用）→
  存进 env（+1）。`cl->env = env` 用 FRESH/INIT 存储（所有权转移，不再 +1）。
- 闭包/环境都是 `@` 值：作用域释放、赋值、`return`、`move`、全局根全部复用
  现有 `@T` 机制；不新增运行期函数。
- 环检测（设计 §7.2）：`c6_gc_flush_descs()` 里对每个用到签名 SIG 的字面量，
  对每个被捕获的托管类型 T 加边 `closure_<SIG> → T`；`closure` 节点用签名
  哈希命名，env 节点不进图。跨 TU 走现有 `.c6.gcgraph`。
- C94 集合补齐：`own`、`weak`、含托管字段结构体、方法接收者（在
  `c6_method_register()` 里给函数类型的第一参数打 `a.c6_receiver`，
  `sym_push_params()` 的 `sym_copy` 会把它带到局部参数符号上）。

测试：

- `tests/closure.c`：
  - 返回闭包后 `@T` 捕获让对象存活；`f = NULL` 后 `live()` 归零；
  - 捕获闭包（嵌套引用）的 +1；
  - `struct Holder { closure int() f; }` 放进 `new Holder`，释放后归零；
  - `move(f)`、全局 `closure S g = NULL`（`tests/exit/closure_global.c`）。
- `tests/errors/closure_cycle.c`：C66 环。
- `tests/errors/closure_capture.c`：`own`/`weak`/含托管结构体/接收者。
- `tests/gc.c` 不动；`tests/closure.c` 里用 `__c6_gc_live()` 做卫生检查。

验收：`make build && make selftest && make test-plain`。

风险与对策：

- env 字段偏移与描述符偏移必须一致（都从 `caps[i].off` 来）。
- 捕获 `@T` 的初始化是 `C6_GC_INIT` 存储；不要读 env 的旧字节。
- 闭包对象释放先释放 env，env 再释放字段；描述符的字段顺序不影响正确性，
  但测试要覆盖嵌套 `@T`。
- 环边会带来保守误报：在 `tests/errors/` 里明确写一条「同签名字面量保守
  报环」的用例，文档 §7.2 已说明。

---

## P3 嵌套字面量、TYPE 捕获、全部控制流/签名特性

目标：设计 §4.5、§5.5、§5.6、§6.2/§6.6 的全部行为。

改动点：

- 嵌套扫描的 `scan_base` 语义（设计 §5.5）：嵌套体用外层 `scan_base`；
  内层体自己捕获外层体局部/形参的行为在真实编译时再算。
- 扫描模式的嵌套字面量：值类型按 V1 函数指针；不入队；不领名字；
  `func_vt`/`func_var`/`c6_func_sym` 切到内层签名（临时 Sym），扫完恢复。
- TYPE/ZERO 捕获：`sizeof`/`typeof`/`_Alignof` 下，块作用域 `typedef` 记
  `C6_CAP_TYPE`（体里压 `VT_TYPEDEF` 别名）；值名字记 `C6_CAP_ZERO`（体里压
  该类型的零值别名，支持 `sizeof(外层数组)`）。扫描维护 `c6_scan_uneval`
  深度来决定 VALUE/ZERO；两者都不放 env 字段；VLA 仍 C94。
- 在扫描模式抑制/模拟：`defer`/`errdefer` 体照常扫；`try`/`catch` 体照常扫；
  `switch`/`for`/`while`/`do`/`foreach`/语句表达式/`goto`/标签。
- 体里 `defer`/`err`/`try`/`catch`/`return` 的清理链：真实编译时走现有
  `gen_function`，捕获别名只是普通左值。
- 变参捕获字面量：`ftype_code` 的形参链 = env + 固定参数，`func_var = 1`；
  体里 `va_start`/`va_arg` 正常。
- `!error`：`lit->ftype.ref->f.func_fallible = 1`，`ftype_code` 同步；
  调用点 `c6_value_fallible` 来自代码字段类型；`try`/`catch` 测试。
- struct 返回：代码签名返回 struct；调用点 sret 在 env 之前；测试。
- `static inline` 体里的字面量：`c6_lit_flush()` 的 fixpoint 循环已覆盖。
- `async` 体内创建闭包：env 在堆上，捕获规则不变；`weak/own` 仍不能捕获
  （C94），`shared @T` 可以（原子 +1）；闭包本身不能跨任务（C96）。

测试：

- `tests/closure.c`：
  - 嵌套字面量：内层捕获外层捕获名、外层局部、内层参数遮蔽；
  - `sizeof(捕获名)`、`typeof(捕获名)`；
  - 体内 `defer`/`err`/`try`/`catch`；
  - 变参捕获字面量用 `va_arg`；
  - `!error` 闭包 + `try`/`catch`；
  - struct 返回；
  - async 体内创建并调用闭包（不跨任务）；
  - `static inline` 体里创建闭包。
- `tests/errors/closure_control.c`：体内 `goto` 外层标签、`await` 等。
- `tests/errors/closure_vararg.c`：C92。

验收：`make build && make selftest && make test-plain`。

风险与对策：

- 扫描模式下的 `defer` 不能登记，但 `defer` 体必须扫；把
  `c6_defer_parse()` 拆成「解析语句」和「登记」两半，扫描只调用前半。
- `try`/`catch` 在扫描时 `c6_func_sym` 必须是字面量自己的临时符号，
  否则 `try` 会按外层函数的 `!error` 误判。
- 标签栈：扫描压一层新作用域，外层标签不可见（与真实编译一致）。
- `c6_implicit_ret0`/`c6_last_stmt_leaves`/`rsym` 成对保存恢复。

---

## P4 函数指针包装、跳板、头文件、跨 TU、C99

目标：`closure S c = fp;`（非变参）、非捕获字面量放进闭包槽、
`-emit-header` 写出闭包结构体、两 TU 互传、C99 后端全绿。

改动点：

- 转换点（设计 §3.5）：在 `vstore()`、`gen_assign_cast()`、
  `gfunc_param_typed()`、`c6_return_value()` 里，**先**做
  `c6_closure_convert(&dst_type)`，再走 `@T` 存储钩子。
  - 源是函数指针、目标是同签名闭包 → 包装；变参 → C92；
  - 源是闭包、目标是函数指针 → C91；
  - 源是闭包、目标是 `void *` → C91；
  - 显式强转 `(closure S)fp` 走同一函数。
- 跳板：`C6TrampPending`（name/hash/ftype_code），按签名去重，TU 末尾由
  `c6_closure_flush_tramps()` 发；定义体：

  ```c
  static RET __c6_tramp_<hash>(void *__c6_env, P0 a0, …)
  {
      return ((struct __c6_env_ptr_<hash> *)__c6_env)->__c6_fn(a0, …);
  }
  ```

  env 对象 `struct __c6_env_ptr_<hash> { RET (*__c6_fn)(P…); }`，无托管字段，
  描述符 0。
- `-emit-header`：
  - `c6_hdr_type()` 识别闭包：写 `struct __c6_closure_<hash> *`；
  - 收集用到的闭包（类似 `c6_hdr_extras`），在函数原型前每个 hash 发一次；
  - 结构体内容：`RET (*__c6_code)(void *__c6_env, P…); void *__c6_env;`；
  - 切片/元组 typedef 先于闭包结构体（参数里可能有）；
  - `typedef closure S X` 写成 `typedef struct __c6_closure_<hash> *X;`。
- `import`：重放原型时 `c6_closure_get()` 重新建同名同布局类型；
  跨 TU 的闭包参数/返回值按指针传递。
- C99：
  - `c99-gen.c` 的声明区发闭包结构体（每个 hash 一次），类型打印成
    `struct __c6_closure_<hash> *`；
  - env 结构体不出现（大小/偏移是前端常量）；
  - `c99_runtime_names[]` 不需要新符号；
  - 跳板 / 调用 / struct 返回 / 变参都是普通 C。
- 静态初始化（C95）：文件作用域/`static` 局部里，除 `NULL` 外的闭包值
  报 C95（即使是非捕获字面量的包装，因为包装要分配）。

测试：

- `tests/closure.c`：函数指针变量 → 闭包、非捕获字面量 → 闭包、
  `(closure S)fp`、包装后的闭包返回/传参、`live()` 归零。
- `tests/errors/closure_convert.c`：C91、C92、C95。
- `tests/abi/closure.c` + `tests/abi/closure_plain.c`：纯 C 侧只按
  `struct __c6_closure_h *` 保存/传递，不调用；退出码一致。
- `tests/tu/closure.c`：闭包作参数/返回值跨 TU。
- `-emit-header` 用例：生成的 C 头用 `gcc -c` 单独编译（可放在
  `tests/tu/` 的脚本里，或复用现有 header 测试手法）。
- `tests/c99/closure.c`：捕获、包装、跳板、`!error`、struct 返回、
  `__c6_gc_live()`；`gcc -std=c99 -pedantic -Wall -Wextra -Werror`。
- `tests/exit/closure_global.c`：全局闭包根在退出时释放。

验收：P1–P3 的命令 + `BACKEND=c99 tests/run-tests.sh` + `make c99test`。

风险与对策：

- 转换必须在 GC 存储钩子之前，否则函数指针会被当对象 incref。
- `gen_assign_cast` 的调用点很多，先在 `vstore()` 与 `gfunc_param_typed()`
  两个必经点做，再用错误用例找漏。
- C99 结构体声明顺序：先收集，再统一发，不能边遍历边发。
- `-emit-header` 的闭包结构体和切片/元组 typedef 的先后顺序要有测试。

---

## P5 诊断补齐、文档、全量回归/自举

- 对照设计 §9 把 C90–C98 的消息逐条实现，`tests/errors/` 每条至少一个
  `// EXPECT`。
- `grammar.md`：§3.10 加「V2 设计预览（未实现）」，列出 `closure` 产生式、
  转换表、C90–C98 指针；正文仍以 V1 为准。
- `plan.md`：阶段表加一行「29 捕获式闭包（V2）」，状态「设计定稿，未实现」；
  「函数字面量的设计取舍」里把「捕获版是胖指针」改成「托管引用 + 隐藏 env」。
- `README.md`：函数字面量一节加 V2 说明和一个小例子（标注未实现）。
- `gc-design.md`：非目标里「捕获式闭包」改成「V2 设计已定稿，见
  closure-design.md，尚未实现」。
- 全量验收：

  ```sh
  make build
  make selftest
  make test-plain
  BACKEND=c99 tests/run-tests.sh
  make c99test
  make selfhost
  make c99selfhost
  ```

- 自查：V1 的 `tests/fn_lit.c`、`tests/errors/fn_lit.c`、`tests/abi/fn_lit*`
  完全不改；`make test-plain` 零差异。
