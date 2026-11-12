# 实现说明

> **状态**：现行实现 · **索引**：[文档索引](../README.md) · **设计动机**：[plan.md](plan.md) · **阶段状态**：[status.md](../history/status.md)
>
> 这一篇回答「代码在哪、为什么这么切」：运行时状态放在哪、为什么方法单趟编译要先声明、
> 元组与切片的 ABI 怎么定，以及每个语法对应哪个实现函数。语法本身见
> [grammar.md](../reference/grammar.md)，用法见 [manual/](../manual/)。

---

## 运行时状态与 ABI

- 运行时状态（错误槽、插值构造器、协程池、子线程链表）是**每线程**一份，
  通过 `pthread_key` 查找而不是 `__thread`：`tcc -run` 不支持 TLS。`spawn`
  出去的线程拿到自己的一份，所以线程里可以照常写 `err`/`co`。`defer` 的
  清理状态不在其中——它在函数帧里，所以线程里、ISR 里都一样。
- 方法必须在调用点之前声明或定义（TCC 单趟编译的固有限制）；接口一致性检查发生在
  第一次转换点，因此方法也要在转换点之前可见。
- 跨 TU：模块就是文件，`import a.b;` 按路径读声明；接口的方法声明放头文件里，
  vtable 由每个 TU 按需自行生成。
- 方法符号是 `<结构体>_<方法>`，源码里写的名字另外发一个弱别名，所以
  `f(p)` 这种普通调用照旧可用，不同类型也可以有同名方法；`static` / `inline`、
  `__` 开头的名字、系统头文件里的声明和匿名结构体的方法保留原名。
- 裸 tag 类型名在 `parse_btype()` 里解析：普通命名空间命中（变量/函数/枚举常量/typedef）
  就按老 C 语义走，否则再看 tag 命名空间；解析结果与写 `struct TAG` 完全同一个
  `CType`，所以 `sizeof`、方法注册、切片元素检查都自动一致。因为 tag 与普通符号
  分属两个命名空间，**同名时必须写 `struct`**（`struct stat` + `stat()` 就是例子）。
- 枚举成员的限定名 `Color.RED` 走的是同一套点号解析：声明侧在 `struct_decl()`
  里把成员**另外**注册成带点的常量名，并记住哪些裸名被两个枚举抢过（此后裸名
  报错）；引用侧在 `unary()` 已有的 `pkg.member` 分支里先按带点名字查、查不到
  再按包名 mangle。因为 `Color` 同时也是类型名，`decl()` 在条件位置向前看一个
  记号：`Tag.` 一定不是声明，整条交给表达式解析（否则 `if (Color.RED)` 会被
  当成声明）。
- 变量初始化检查在**定义点**做，不做数据流分析；`static` 变量与 VLA
  豁免（前者由 C 保证清零，后者标准 C 禁止写初始化器）。
- 元组复用 `VT_STRUCT` 和内部 tag `__c6_tuple_N`，布局在构造类型时按自然对齐
  算好（不读 `pack_stack_ptr`），因此 `#pragma pack`、成员顺序、对齐都不会改变
  它的 ABI；元素名 `__c6_t0`…只为实现细节，对外只有 `t[i]`。
- 切片是普通 `static`/自动对象，没有隐藏状态也没有 GC（托管引用 `@T` 是
  另一回事，它的计数在对象头里）：越界检查由
  `libtcc1` 的 `__tcc_slice_index` / `__tcc_slice_sub` 完成，元素类型不参与
  ABI，因此两个 TU 只要都写 `T[:]` 就能互通。

---

## 实现位置

每个语法对应的实现函数与落点文件（原来在 `grammar.md` §11；语法规范不再重复这张表）。

| 语法 | 实现 | 位置 |
|---|---|---|
| `module` / `import` | `c6_module_parse` / `c6_module_import` / `c6_pkg_name`（点号名拼成 `a__b`）/ `c6_module_open` + `c6_module_graded` + `c6_module_root`（按路径分级查找）/ `c6_mod_imported`，`unary()` 里的点号解析；`package` 只剩 `c6_package_parse` 的迁移提示 | `tcc/tccgen.c` |
| `pkg.member` | `unary()` 的 `tok_identifier` 分支（逐段增长前缀 + `sym_find_raw`） | `tcc/tccgen.c` |
| 结构体方法 | `c6_method_register`（在 `decl()` 里注册）/ `c6_method_symbol`（`<Tag>_<name>` 符号名）/ `c6_method_plain_decl` + `c6_method_alias`（源码名弱别名）/ `c6_method_lookup` / `c6_has_field` / `c6_method_call_prep`，`unary()` 的 `->` 分支 | `tcc/tccgen.c` |
| `interface` | `c6_interface_parse`、`c6_auto_impl_ensure` / `c6_check_method` / `c6_emit_auto_impls` / `c6_iface_convert` | `tcc/tccgen.c` |
| 每 TU 状态复位 | `c6_tu_reset`（在 `tccgen_compile` 开头调用） | `tcc/tccgen.c` |
| `test` | `c6_test_parse`、`tcc_run_tests` | `tcc/tccgen.c`、`tcc/tccrun.c` |
| `defer` / `errdefer` | `c6_defer_parse`、`c6_defer_begin`、`c6_defer_func_end`（运行期状态在函数帧里，见 8.2；`libtcc1` 里没有 defer 运行时） | `tcc/tccgen.c` |
| `err` | `c6_err_parse` / `c6_emit_propagate` | `tcc/tccgen.c` |
| `co` | `c6_co_parse`、`__tcc_co_spawn` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| `thread` | `c6_thread_decl`、`__tcc_th_spawn` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| `spawn` | `c6_spawn_parse` / `c6_sp_parse` / `c6_sp_emit` / `c6_sp_flush`、`__tcc_sp_args` / `__tcc_th_start` / `__c6_th_join_all` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| `thread` 句柄 | `c6_thread_decl` / `c6_thread_defer_release`、`__tcc_th_spawn` / `__c6_th_release` / `__c6_thread_*` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c`、`tcc/c99-rt.inc` |
| 句柄方法 | `c6_method_lookup` + `c6_task_head_ptr`（`t->wait()`）、预置方法声明（`h->join()`） | `tcc/tccgen.c`、`tcc/tccpp.c` |
| 软关键字 | `c6_soft_kw` / `c6_soft_kw_shadowed`（`block()` 与 `decl()` 两个入口） | `tcc/tccgen.c` |
| 每线程状态 | `tcc_th()` / `tcc_th_join_children`（`pthread_key`） | `tcc/lib/libtcc1.c` |
| `task` | `c6_task_decl` / `c6_task_check_handle` / `c6_task_defer_destroy` | `tcc/tccgen.c` |
| 顶层调度（就绪链表 + 一次 `epoll_wait()`，长度只受内存限制；注册集按 fd 索引 + `EPOLLONESHOT` 重装填，唤醒走下标 O(1)） | `tcc_co_pump` / `tcc_co_step_once` / `tcc_co_link` / `tcc_co_unlink` / `__c6_co_run` | `tcc/lib/libtcc1.c` |
| `!error` | `c6_parse_fallible_suffix` / `c6_apply_fallible`，`FuncAttr.func_fallible` | `tcc/tccgen.c`、`tcc/tcc.h` |
| `f".."` 切分 | `parse_fstr_string` / `fstr_lex` / `fstr_add_lit` / `fstr_unquote`，`TOK_C6_FSTR` → `TOK_C6_FSTR_BEG` … `TOK_C6_FSTR_END` | `tcc/tccpp.c`、`tcc/tcctok.h` |
| `f".."` 下降 | `c6_fstr_expr` / `c6_fstr_lit` / `c6_fstr_piece`（`unary()` 里的 `TOK_C6_FSTR_BEG` 与 `'@'`） | `tcc/tccgen.c` |
| `@f".."` 的托管结果 | `C6_GC_FRESH` + `C6_GC_ATOMIC` 的 `__c6_fmt_end` | `tcc/tccgen.c`、`tcc/lib/libtcc1.c` |
| f-string 运行时 | `__c6_fmt_beg` / `__c6_fmt_l` / `__c6_fmt_s` / `__c6_fmt_c` / `__c6_fmt_ch` / `__c6_fmt_sl` / `__c6_fmt_i` / `__c6_fmt_u` / `__c6_fmt_p` / `__c6_fmt_b` / `__c6_fmt_end` / `__c6_str_len` | `tcc/lib/libtcc1.c` |
| f-string 的 C 后端 | 同一组 `__c6_fmt_*` 的 C 实现、`c99_runtime_names[]` 里的声明 | `tcc/c99-rt.inc`、`tcc/c99-gen.c`、`tcc/tccpp.c` |
| `try` | `c6_try_parse`（`unary()` 的前缀分支） | `tcc/tccgen.c` |
| `catch` | `c6_catch_parse`（`expr_catch()` 层，位于 `expr_cond()` 之下） | `tcc/tccgen.c` |
| 宏 `expr`/`stmt`/`ident`/`type` + `__local` | `c6_macro_def_parse` / `c6_role_of`（文件作用域的软关键字入口） / `c6_macro_body_capture` / `c6_macro_check_body` / `c6_hygiene_body` / `c6_scan_locals` / `c6_macro_wrap` / `c6_param_role` | `tcc/tccgen.c`、`tcc/tccpp.c`、`tcc/tcc.h` |
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
| 运行期全部 | `__c6_gc_new` / `__c6_gc_store` / `__c6_gc_release` / `__c6_gc_drop` / `__c6_gc_move` / `__c6_gc_incref` / `__c6_gc_weak_store` / `__c6_gc_drain_budget` / `__c6_gc_shutdown`，以及 `async`/`spawn` 的任务槽归还（`__c6_rel_NAME`）| `tcc/lib/libtcc1.c`、`tcc/c99-rt.inc` |
| `foreach` | `c6_foreach_parse` + `c6_foreach_binder`（`block()` 的 `TOK_C6_FOREACH` 分支；下一个记号不是 `(`（或 `foreach` 是作用域里已知的标识符）时 `unget_tok` 退回表达式语句）、`c6_common_int_type` / `c6_is_int_type`、`c6_iter_proto_of` / `c6_iter_setup` / `c6_iter_call`（迭代器协议：结构化识别、静态调用或 vtable 分派）；数组/切片的隐藏槽复用 `c6_slice_spill`，循环体走 `lblock`；文件作用域诊断在 `decl()` | `tcc/tccgen.c` |
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
