# 实施状态与验收

> **状态**：历史记录（P0–P8 全部完成，保留作验收依据） · **索引**：[文档索引](../README.md) · **设计**：[plan.md](../design/plan.md) · **变更史**：[CHANGELOG.md](../../CHANGELOG.md)
>
> 这一篇是从 `plan.md` §11 抽出来的实施状态表：每个阶段做了哪些函数、落在哪个文件、
> 用什么用例验收。它记录「做到哪一步」，不描述「应该怎么做」——设计动机见
> [plan.md](../design/plan.md)，语法见 [grammar.md](../reference/grammar.md)。
>
> 文中的规模数字（模块数、`test` 块数、自检条数）是**当时阶段的快照**，现状以
> `make stdtest` / `make std-tour` 的输出为准；文档里不再另行硬编码这些数字。

## 构建与运行

```bash
cd tcc && make clean && make -j$(nproc) EXTRA-DEFS=-DC6_EXT CFLAGS="-g -O0"
./tcc/c6 -B. -run file.c6               # .c6 后缀开启扩展（唯一开关）
./tcc/c6 -B. -test file.c6              # 运行 test 块
```

| 阶段 | 状态 | 关键实现位置 |
|---|---|---|
| 0 `C6_EXT` 门控 | ✅ | `tcctok.h`（新 token 追加在文件末尾，避免破坏 i386 asm 关键字）、`libtcc.c`（`.c6` 后缀逐 TU 开启）、`tccpp.c`（`__C6__`、未开启时把新关键字还原为普通标识符） |
| 1 `defer` | ✅ | `tccgen.c`：`c6_defer_parse` / `c6_defer_begin` / `c6_defer_func_end`（静态清理点 + 每函数派发器；运行期状态在函数帧里，见第 30 行）、`lib/libtcc1.c` 里没有 defer 运行时 |
| 2 `errdefer`/`err` | ✅ | `c6_errdefer_parse` / `c6_err_parse`、`__tcc_err_set/get/clear`；`err` 直接返回哨兵 `-1`，转发由 `try` 负责 |
| 2b `T!error` / `try` / `catch` | ✅ | `c6_parse_fallible_suffix` / `c6_apply_fallible`（`FuncAttr.func_fallible`）、`c6_try_parse`（`unary()` 前缀分支）、`c6_catch_parse`（`expr_catch()` 层）、`c6_emit_propagate`（与 `err` 共用清理+哨兵路径）、`__tcc_error_take` / `__tcc_err_flag_peek` |
| 3 `test` / `-test` / `-test-filter` | ✅ | `c6_test_parse`、`tccrun.c:tcc_run_tests`、`libtcc.c` 选项 |
| 4 `module`/`import`/`mod.member` | ✅ | `c6_module_parse` / `c6_module_import` / `c6_module_open`（分级路径）/ `c6_pkg_name` / `c6_mod_imported`、`c6_mangle_tok`（`NAME__sym`）、`unary()` 里的逐段点号解析与 `sym_find_raw`；`package` 只剩迁移提示 |
| 4b 结构体方法 | ✅ | `decl()` 里 `c6_method_register`、`c6_method_symbol`（符号名 `<Tag>_<name>`，`static`/`inline`/`__` 开头/系统头/匿名 tag/包内方法不改名）、`c6_method_plain_decl` + `c6_method_alias`（源码名发布成弱别名，定义处用 `put_extern_sym2` 打上同一地址）、`c6_method_source_name`（`__func__` 显示源码名）、`c6_method_lookup` / `c6_has_field` / `c6_method_call_prep`、`unary()` 的 `->` 分支、`c6_self_*` 隐藏接收者（sret 之后）；`BufferedFile.sys_header`（`tccpp.c` 按 include 路径标记系统头） |
| 5 `interface` | ✅ | `c6_interface_parse`（生成 `struct NAME_vt` + 胖指针 `struct NAME`）、`c6_auto_impl_ensure` / `c6_check_method`（惰性校验）、`c6_emit_auto_impls`（TU 末尾生成 `static` vtable）、`c6_iface_call_prep`（调用点改写）、`c6_iface_convert`（隐式/显式转换） |
| 6 跨 TU | ✅ | 模块即文件，`import a.b;` 按路径读声明；接口与方法声明放头文件，每个 TU 各自生成 vtable；方法符号按接收者取名，所以同名方法跨 TU 也不需要命名空间；`c6_tu_reset` 在每个 TU 开头丢弃上一 TU 的模块状态/接口/方法/待生成 vtable |
| 7 宏 `expr`/`stmt`/`ident`/`type` | ✅ | `tccgen.c`：`c6_role_of` / `c6_macro_def_parse`（文件作用域的软关键字入口，读 `ROLE NAME (ROLE param, ...) { body }`）；`tccpp.c`：`c6_macro_body_capture`（`{}` 配对捕获，`##` → `TOK_PPJOIN`）、`c6_macro_check_body`（`#`/`##` 操作数必须是 `ident` 角色；`expr` 只能一个表达式）、`c6_hygiene_body` / `c6_scan_locals` / `c6_local_name`（自动卫生重命名 `<宏名>__<变量>__h<N>`，`__local` 仍可手动标注）、`c6_macro_wrap`（`expr` → `( … )`、`stmt` → `{ … }`、`ident`/`type` 原样）、`c6_param_role`、`macro_arg_subst` 给 `expr` 实参加括号；`#macro` 只剩一条迁移诊断 |
| 10 变量必须初始化 | ✅ | `tccgen.c`：`c6_check_var_initialized`，在 `decl()` 的对象定义分支（`has_init` 判定之后、TCC 把暂定全局改成 `extern` 之前）调用；`extern`/函数/`typedef`/`VLA`/`static`/alias 豁免；诊断使用 mangle 前的原始标识符 |
| 11 切片 `T[:]` | ✅ | `parse_btype()` 的 `case '['`（只认 `[:]`，其余 `unget_tok` 还原）→ `c6_slice_get`（按元素类型规范化生成内部 struct，固定布局、不走 `struct_layout`，因此 `#pragma pack` 无效）；`c6_slice_subscript`（`unary()` 的 `[` 分支，下标与区间）、`c6_slice_spill`（把基值落到本地 `{data, len}` 对）、`c6_slice_convert`（数组→切片：`vstore`/`gen_assign_cast`/强转/`decl_initializer`）；`type_to_str` 打印 `T[:]`；运行期 `libtcc1.c` 的 `__tcc_slice_index` / `__tcc_slice_sub`（越界空指针写陷阱） |
| 12 `async`/`await` 无栈协程 | ✅ | `parse_btype()` 的 `TOK_C6_ASYNC`（`FuncAttr.func_async`）→ `c6_async_declare`（用合成 token 在文件作用域声明 `struct NAME_task`）；`c6_async_definition` 把函数体按 `int __c6_body_NAME(void *task, void *frame)` 编译，`c6_async_func_begin` 生成恢复分发序言（非 0 状态就 `__c6_co_restore` + 跳恢复点）、`c6_async_params_copy` 把形参落成普通局部、`c6_async_func_end` 生成恢复分发器、`c6_async_finish` 写 `val` 并返回 `KIND_DONE`；`c6_async_call`（`unary()` 的 `tok_identifier` 分支）把调用改写成 `__c6_co_new` + 实参拷贝；`c6_await_parse`（`unary()` 前缀分支）+ `c6_async_suspend` 把错误槽 park 出去（帧住在任务里，不用搬，清理状态跟着帧走）；运行期 `libtcc1.c` 的 `__c6_co_new`（一次分配任务结构 + 帧）/ `__c6_co_await` / `__c6_co_suspend` / `__c6_co_restore` / `__c6_co_finish` / `__c6_co_wait` / `__c6_co_destroy`（按需 malloc + 手写 `select()`，不依赖 libc）；预置声明在 `tccpp.c` 的 `__TCC_ASYNC_DEFINED` 段 |

| 13 `co` / `task` 软关键字 | ✅ | `co`/`spawn`/`task` 不进 `tcctok.h`：词法阶段仍是标识符，`block()` 里 `tok >= TOK_IDENT` 且 `c6_soft_kw(t)` 命中、名字没有被 `VT_TYPEDEF` 占用时按关键字分派（`c6_soft_kw_shadowed`），所以 `int spawn = 1;`、`spawn(x);`、`typedef int co;`、`spawn:` 都还成立；`c6_co_parse`（`decl()` 与 `block()` 两个入口——单条语句不走 `decl()`）+ `__tcc_co_spawn`；`c6_task_decl` / `c6_task_check_handle`（`struct NAME_task` 的 `head` 必须是 `struct __c6_task`）/ `c6_task_defer_destroy`（合成一个作用域清理点，同手写 `defer`）；运行期 `libtcc1.c` 的顶层就绪链表（`snext`/`busy`/`waiting`）与 `tcc_co_pump` / `tcc_co_step_once` / `tcc_co_link` / `tcc_co_unlink` / `__c6_co_run`，`__c6_co_wait` 也改成先推进顶层任务；`async` 体里的 `err` 走 `c6_async_propagate`（`KIND_ERR`），并禁止 `async` 方法是方法；预置声明同步加 `__tcc_co_spawn` / `__c6_co_run` |
| 14 `spawn` = OS 线程 | ✅ | `c6_spawn_parse` 只在 `.c6` 的翻译单元里生效：实参按声明形参布局拷进 `__tcc_sp_args(size)` 的堆块（对齐循环与跳板共用同一套 `align`），跳板 `static void *__c6_sp_NAME(void *)` 在 `spawn` 调用点用 `external_sym(VT_STATIC\|VT_EXTERN)` 惰性声明、由 `c6_sp_flush()` 在 TU 末尾定义（`c6_sp_pending` 链表，`c6_tu_reset` 释放），所以头文件里的原型不产生任何代码；运行期 `libtcc1.c` 新增 `__tcc_th_start`（`pthread_create` + 把 `TccThRec` 挂到本线程链表）、`tcc_th_tramp`（解开参数块、调用、释放）、`__c6_th_join_all` / `tcc_th_join_children`；`gen_function` 在 `main` 末尾插 `__c6_th_join_all()`，`tccrun.c:tcc_run_tests` 也调一次，`-run` 不会在线程还在跑时释放 JIT 内存；每线程状态（错误槽 / 插值构造器 / 协程池）从 `__thread` 改成 `pthread_key_create` + `pthread_getspecific`（`-run` 不支持 TLS）；`defer` 不在其中，它的状态在函数帧里 |
| 15 裸 tag 类型名（省略 `struct`） | ✅ | `tccgen.c`：`parse_btype()` 的 `default` 分支——`typespec_found` 为 0、`sym_find(tok)` 未命中（普通命名空间优先）时试 `struct_find(tok)`，命中则把 tag 符号的 `type.t` / `ref` 交给 `basic_type2`，与 `struct/union/enum TAG` 走完全同一条路径（所以 `sizeof`、方法注册、`c6_tag_is_global` 的切片元素检查、`type_to_str` 自动一致）；`ignore_label` 时 `TAG:` 仍退回标签（与 typedef 路径同一个 guard）；不隐式前置声明，也不支持定义处省略 `struct`；只在 `.c6` 生效 |
| 16 `foreach` | ✅ | `c6_foreach_parse` + `c6_foreach_binder`（`block()` 的 `TOK_C6_FOREACH` 分支；下一个记号不是 `(`（或 `foreach` 是作用域里已知的标识符）时 `unget_tok` 退回表达式语句，所以 `foreach` 仍可当标识符）；词法是 `tcctok.h` 末尾追加的硬关键字，未开启时（`.c` 文件）由 `tccpp.c` 的还原循环改回普通标识符（区间同步扩到 `TOK_C6_FOREACH`）；数组/切片的隐藏槽直接复用 `c6_slice_spill`（`{data, len}` 对，与切片运行期布局相同），区间把上界落进隐藏整数槽、下标绑定当计数器；`i`/`v` 是 `sym_push` 出来的普通局部，循环体走 `lblock`，所以 `break`/`continue`/`defer`/`await` 与 `for` 一致；`size_t` 下标用 `c6_size_type`，区间用 `c6_common_int_type`（C 的通常算术转换子集）；文件作用域的 `foreach` 在 `decl()` 里报错；头有两种写法：声明式 `foreach (T v = a)` / `foreach ((T i, T v) = a)` / `foreach (T i = lo:hi)`（`c6_foreach_binder` 借 `parse_btype` + `type_decl` 解析，绑定类型就是体里的类型，循环状态仍住隐藏槽，所以下标也是每轮的副本）；旧的绑定表 `foreach \|i, v\| a` 已删除，语句分支只认 `(`，命中 `\|` 时给出迁移提示（C121）；迭代器协议（`has()`/`next()`，含结构体值/指针/接口值）见里程碑 30 |
| 17 元组 `(T1, T2, …)` | ✅ | `tccgen.c`：`parse_btype()` 的 `case '('` → `c6_tuple_btype` / `c6_tuple_speculate`（保存/重放 token，区分元组类型与老 C 的括号表达式）、`c6_tuple_get`（校验元素并规范化生成内部 tag `__c6_tuple_N`，布局在构造时按自然对齐算好，不走 `struct_layout`）、`c6_tuple_ctype` / `c6_tuple_of_type` / `c6_tuple_ctype`；`unary()` 的 `case '('` 处理元组强转/复合字面量/`sizeof`/`_Alignof`、`c6_tuple_subscript` 处理 `t[k]`（常量下标）；`decl()` 开头的 `c6_tuple_lead_kind` + `c6_tuple_destructure` 实现解构声明/赋值（`_` 占位符丢弃元素）；`c6_return_value` 支持 `return {…};`；`c6_apply_fallible` 放开 struct/union/元组作 `!error` 返回类型，失败路径用 `c6_zero_local` 清零；`c6_async_type_tokens` 把元组回放成 `(T1, T2)` 而不是内部 tag；解构语句在 `block()` 里也认一次（无花括号的 if/while/for/switch 单条语句体、语句表达式、`defer` 单条语句体不走 `decl()`），赋值形式在 `c6_tuple_destructure()` 里就地解析（作用域弹出后隐藏临时变量就没了） |
| 18 `module` + `import` 加载 | ✅ | `tccgen.c`：`BufferedFile.c6_module` / `c6_decl_seen`，`decl()` 文件作用域的 `module NAME;` 软关键字 → `c6_module_parse`（点号前缀、必须首条声明、一个文件一个、`main` 禁止）；`c6_mod_name()` 给文件作用域声明加 `NAME__`（块作用域/`static`/`test`/`__` 开头/普通 `#include` 不加），方法经 `c6_method_symbol` 自然变成 `<NAME__Tag>_<meth>`；`sym_find` / `struct_find` 先试模块前缀再试普通 C 名，`c6_import_lookup` / `c6_import_tag_lookup` 兜底 imports；`parse_btype` 的 `c6_qualified_type` 支持 `NAME.Type`（先 speculative 扫描 typedef/tag，失败原样回退）；`c6_module_import` 按当前文件目录 → `-I` → 当前目录依次找 `NAME.c6`、`NAME.c`（点号 `a/b.c6`、`a/b.c`，`.c6` 优先），用 `decl(VT_CONST)` 重放：函数原型 + `skip_or_save_block`、全局 `extern` + `c6_skip_decl_tail`、跳过 `static`/`test`，循环 import 报错，找不到文件保持旧的命名空间行为；`-emit-header` 用 `C6HeaderDecl` 记录导出，`c6_hdr_type` / `c6_hdr_struct_body` / `c6_emit_header` 写出普通 C 头文件（切片/元组生成 `NAME__slice_N` / `NAME__tuple_N` typedef） |
| 19 托管引用 P0/P1（限定符、分配、存储、释放） | ✅ | `tcc.h`：`CType.gq` + `C6_GQ_*` + `C6_GCM_*`；`tccgen.c`：`parse_btype()` 的 `@` 前缀、`c6_gc_new_expr`、`c6_gc_store_hook`（`vstore()` 里）、`c6_gc_site`（合成一个清理点）、`c6_gc_plain` / `c6_gc_mode` / `c6_type_gq`；运行期 `libtcc1.c` 的 `__c6_gc_new` / `__c6_gc_store` / `__c6_gc_release` / `__c6_gc_drop`；`CType.gq` 只当缓存、可信副本在 pointee 上（设计 §2.0） |
| 20 描述符与字段释放 P2 | ✅ | `c6_gc_collect()`（struct 定义完成时收集字段表：`C6GcField.off/kind/count/stride`）、`c6_gc_size_of()`/`c6_gc_desc_for()`/`c6_gc_desc_decl()`/`c6_gc_emit_desc()`/`c6_gc_flush_descs()`（TU 末尾发射 `static const struct __c6_gc_desc __c6_gc_d<fnv>_<n>`，跨 TU 不重名）；运行期按描述符逐字段释放（数组逐元素、嵌入递归） |
| 21 `weak` P3 | ✅ | 弱槽注册表（全局自旋锁，冷路径）+ `__c6_gc_weak_add`/`__c6_gc_weak_unlink`/`__c6_gc_weak_store`（原子 try-incref 升级）；`weak` 不能直接解引用（`vstore`/`unary` 两处检查）；async 里禁 `weak`（`c6_async_in_body`） |
| 22 析构 `drop` P4 | ✅ | `c6_gc_drop_register()`（`c6_method_register()` 里登记）、`c6_gc_is_drop()` / `c6_gc_drop_callee()`（禁止 `p->drop()` / `drop(p)`）、描述符增加 `drop` 指针、运行期 `c6_gc_free_one()` 先跑析构再放字段；全局根用 TU 末尾的 `__attribute__((constructor))/((destructor))`（不用 `atexit`，否则拖出 `__dso_handle`） |
| 23 `shared` 与跨帧/跨线程 P5 | ✅ | 原子与否在对象头（`new shared T`），`__c6_gc_inc`/`c6_gc_drop` 按标志走原子指令；`spawn` 实参块 +1 / trampoline −1（`c6_sp_emit`）、`async` 任务前缀 +1 / `__c6_co_destroy` −1（描述符第 5 个成员 `release` → `__c6_rel_NAME`）；形参是借用（`C6_GQ_BORROW`，E-GC14）；`new shared T` 强制（E-GC15） |
| 24 静态环检测 P6 | ✅ | `c6_gc_collect()` 记 `C6GcField.to_tag`（强引用边、含数组与按值嵌入；`weak`/`own` 不是边），`c6_gc_flush_descs()` 里三色 DFS（`C6GcType.mark`）报 E-GC1；`move(p)` → `__c6_gc_move`（`own` 链“先建后接”的唯一写法）；`move(p)` 之后的空槽再用报 E-GC17（`Sym.gc_moved` + `if`/`while`/`for`/`switch`/`foreach` 的状态合并、`defer` 体隔离，`tests/errors/gc_move.c6` 8 条） |
| 25 任务回卷 P7 | ✅ | 运行期 `C6_CO_KIND_ABORT` / `__c6_co_abort` / `__c6_co_destroy` 先回卷；编译器在**每个 `await` 的恢复点**发 `if (__c6_co_kind(task) == ABORT) { defer_begin(2,0); finish(ABORT); }`（放末尾派发器里的话 `defer_state` 已复位，清理体发不出来） |
| 26 线程与任务句柄 | ✅ | `thread h = spawn f(x);`（`c6_thread_decl` + `c6_thread_defer_release`，运行期 `TccThRec` 引用计数：链表 + 句柄）、`h->join()`/`h->done()`/`h->value()`；任务句柄方法 `t->wait()`/`t->done()`/`t->kind()`（预置方法 + `c6_task_head_ptr` 首字段上转）；`c99-rt.inc` 同步镜像、`tests/thread_handle.c6`、`tests/task_method.c6` |
| 27 函数字面量 `RET (参数) { 体 }` | ✅ | `C6Lit` 队列 + `c6_lit_lead()`（扫描签名 → token 重放判定「函数 + `{`」）、`c6_lit_parse()`（`external_sym` 建 `static __c6_fn_<n>`、`skip_or_save_block` 存体）、`c6_lit_decl_lead()` / `c6_lit_infer_decl()`（`f = …;` 推断声明）、`c6_lit_flush()`（TU 末尾与 `gen_inline_functions()` 同循环 `gen_function()`）、`c6_lit_cap_check()`（捕获诊断 C80，挂在值/typedef/tag 三条符号解析路径）；设计见 [closure-design.md](../design/closure-design.md)、语法见 [grammar.md §3.10](../reference/grammar.md)；`tests/fn_lit.c6` / `tests/errors/fn_lit.c6` / `tests/abi/fn_lit.c6`+`_plain.c` / `examples/fn_lit_demo.c6`；V2（捕获式闭包）见第 29 行 |
| 28 字符串插值 `f".."` / `@f".."` | ✅ | 词法：`tccpp.c` 的 `next()` 把 `f"` 收成 `TOK_C6_FSTR`（保存原始文本），`parse_fstr_string()` 在 `PARSE_FLAG_TOK_STR` 下用 `fstr_lex()` 切成「文本片段 / 插值」并 `begin_macro()` 重放成 `TOK_C6_FSTR_BEG` → (`TOK_STR ','` \| `TOK_C6_FSTR_EXPR` … `TOK_C6_FSTR_EXPR_END`) → `TOK_C6_FSTR_END`（片段后的 `,` 让 `decl_initializer` 走「更大表达式的一部分」分支；`{{`/`}}`/`\"` 由 `fstr_unquote()` 还原）；下降：`tccgen.c` 的 `unary()` 里 `TOK_C6_FSTR_BEG` 与 `'@'` 两个入口 → `c6_fstr_expr(managed)`，逐段 `c6_fstr_lit()` / `c6_fstr_piece()`（按静态类型选 `__c6_fmt_ch/i/u/b/s/sl/p`），字面片段的文本是命名的 `static const char[]`（c99 后端要按名字引用），收口 `__c6_fmt_end(0|1)`；运行期：`lib/libtcc1.c` 的每线程构造器槽（`C6FmtBuf`，按嵌套深度分槽，`__c6_fmt_beg` 压栈、`__c6_fmt_end` 弹栈；借用分支返回本深度的 `out` 缓冲，托管分支拷成 `C6_GC_FRESH\|C6_GC_ATOMIC` 对象）与 `__c6_str_len()`；`c99-rt.inc` / `c99-gen.c` 同步一份 C 实现与声明；格式说明 `{expr;spec}`：词法在 `parse_fstr_string()` 里把插值的第一个未嵌套 `;` 之后切成说明文本（新记号 `TOK_C6_FSTR_SPEC`），`c6_fstr_spec_parse()` 把它编成位（宽度/精度各 10 位 + conv 4 位 + flags），作为 `__c6_fmt_*` 的最后一个实参传给运行时（`__c6_fmt_l` 之外每个 piece 都多一个 `spec` 实参），运行时用 `c6_fmt_num` / `c6_fmt_str_spec` 应用宽度、对齐、补零、精度和进制；浮点（`float`/`double`/`long double`）走 `__c6_fmt_f`，conv 为 `f`/`e`/`g`（默认 `g`），数字由运行时的 `c6_dbl_*` 自己算（`c6_dbl_fixed` 反复乘十取位 + 四舍五入，`c6_dbl_str_sci` 先取十进制指数再做定点；零/nan 不走指数循环），`c99-rt.inc` 里是同一段代码；诊断 C84–C86/C88/C89，重写规则 R24，见 [grammar.md](../reference/grammar.md)；运行时的外部依赖（`calloc`/`free`/`realloc`/`mem*`/`pthread_*`/`select`）全部是 weak 引用，没有 libc 时用 `libtcc1` 里的静态 arena（`C6_HEAP_SIZE`，默认 256 KB）和自带 mem* 顶上、线程退化成单线程，见 `examples/freestanding_fstr.c6`（`make fstest`）；`spawn`/`thread` 只要程序自己实现那 8 个 `pthread_*` 入口即可，且这层必须用普通 C 编译（c6 函数的 defer 前导会递归进 `tcc_th()`）；`examples/freestanding_pthread.c` 是一份真实现（x86-64 Linux 的 clone + futex + `%fs:0` 线程指针 + mmap 栈 + 线程安全的 calloc/free，join 靠 `CLONE_CHILD_CLEARTID`，不能靠 `CLONE_CHILD_SETTID`，见文件注释），配 `examples/freestanding_go.c6`（`make fstest-go`）；宏体、宏实参和 `#x` 字符串化都带着原始文本走（`tcc.h` 的 `TOK_HAS_VALUE` 要认 `TOK_C6_FSTR`，`macro_arg_subst` 复制记号时必须留着 `CValue`，否则记号流会错位——曾经让编译器段错误）；插值里的 `;` 与 `try` 直接报错（C88：前者会静默截断，后者会让运行时构造器留在栈上） |
| 29 捕获式闭包（V2） | ✅ | `parse_btype()` 的 `closure` 软关键字 → `c6_closure_btype()` / `c6_closure_get()`（签名规范文本 + 哈希 tag `__c6_closure_<hash>`，匿名类型、块作用域 tag 报 C90）；字面量在 `c6_lit_parse()` 里用 `c6_lit_scan_*` 拿解析器把体扫一遍求捕获集合（`C6_CAP_VALUE`/`TYPE`/`ZERO`），`c6_lit_cap_check()` 挂在值/typedef/tag 三条符号解析路径上决定「按值进 env」还是 C93/C94；env 结构体按捕获顺序生成，体里的捕获名别名成 `((struct __c6_env_<n> *)__c6_env)->…`，`c6_self_pending` 在 sret/`self` 之后插隐藏 env 首参；调用点由 `c6_closure_call_prep()` 改写成 `__c6_code(__c6_env, …)`；函数指针 → 闭包由 `c6_closure_tramp()` 按签名发一个跳板 + 一字的 env；闭包与 env 是普通托管对象（`c6_gc_collect()` 描述符，env 是 STRONG 字段），签名级保守环边接进 C66；`-emit-header` 发 `{code, env}` 结构体与 typedef、`import` 按签名哈希重建类型、C99 后端是普通 C（`c99-gen.c` 发闭包结构体、跳板/描述符都是普通代码）；诊断 C90–C98（V1 的 C80 文本在 C93 之前照旧打印，既有 `tests/errors/fn_lit.c6` 一行不改）；用例 `tests/closure.c6`、`tests/errors/closure_signature.c6`/`closure_convert.c6`/`closure_capture.c6`/`closure_capture_type.c6`/`closure_cross.c6`/`closure_opaque.c6`/`closure_cycle.c6`、`tests/abi/closure.c6`+`_plain.c`、`tests/c99/closure.c6`、`tests/tu/closure.c6`、`tests/exit/closure_global.c6`；设计见 [closure-design.md](../design/closure-design.md)、验收清单见 [closure-todo.md](../history/closure-todo.md) |
| 30 `foreach` 迭代器协议 `has()`/`next()` | ✅ | `tccgen.c`：`c6_iter_proto_of`（结构化识别：接口值 / `T *` / `T` 值；`has`/`next` 方法来自 `c6_method_of`，真实字段遮蔽方法时不算协议）、`c6_iter_check_has` / `c6_iter_check_next`（签名 C99）、`c6_iter_setup`（隐藏 `has`/`next` 函数指针与 receiver 局部槽，按 `__c6_it_<kind>_<n>` 唯一命名）、`c6_iter_call`（把函数指针与 receiver 装进隐藏槽，再经 token 重放进 `gexpr()`，所以结构体返回/整型提升与手写调用完全一致；接口值取 vtable 槽）；`c6_foreach_parse` 新增迭代器分支：值形态把迭代器复制进隐藏槽（不推进原对象），`T *`/接口形态直接操作原对象，`has() != 0` 为循环条件，`next()` 结果写进值绑定（`_` 时 `vpop`，但仍调用；托管元素则落入隐藏槽以便释放），`continue` 回到 `has()` 判断；C47 提示改为 “no 'has'/'next' iterator”；托管引用：迭代器/元素隐藏槽按声明局部量清零并登记 `c6_gc_site`，`@T` 形参按借用拷贝，含托管字段的 struct 元素由 `c6_gc_value_check` 拒绝；`tests/foreach_iter.c6`、`tests/errors/foreach_iter.c6`、`examples/foreach_iter_demo.c6` |
| 31 `defer` 状态搬进函数帧 | ✅ | 清理点是编译期常量（`C6DeferSite`：id / 作用域深度 / kind / 标签），运行期只剩帧里的两样东西：`arm[C6_DEFER_WORDS]` 位图和 `dtgt`/`dres`（派发过滤 + 恢复点号，清理体每嵌套一层再存一份）；`c6_defer_func_init()` 只在序言里清 4 个 `int` 的位图（不再调用 `__tcc_defer_func_enter`；单趟编译在序言处不知道有没有清理点，所以这 16 字节是每个 c6 函数都付的固定开销），`c6_defer_func_end()` 生成一个逆序扫 site 的派发器，`c6_defer_begin()` 只写两个 `int` 就跳过去——于是没有 TLS/线程键、没有 8192 条记录上限（改成一个函数 128 个清理点、清理体 8 层，超了编译期报错）、ISR/线程/协程/递归同一套规则；`__tcc_defer_*` 五个入口与 `TccDeferRec`/`TccDeferCtx` 从 `libtcc1.c`、`c99-rt.inc`、`c99-gen.c` 的名字表里删掉，协程挂起只剩错误槽三项；向后跳转的 `goto` 在 `stmt()` 里自己发起清理请求（标签处的派发仍保留，向前跳转靠它）；语义见 [grammar.md](../reference/grammar.md) 8.2，约束 C101/C102；用例 `tests/defer_scope.c6`、`tests/defer_reent.c6`、`tests/defer_thread.c6`（裸机跑不了，`examples/boot/runtests.sh` 里跳过）、`tests/async_frame.c6` 第 8 组 |
| 32 枚举成员的限定名 `Color.RED` | ✅ | `struct_decl()` 的枚举循环接受 `'<tag>.<成员>'`：常量另外注册成带点的 `Color.RED`（`tok_alloc_const`），裸名只在没被别的枚举占用时注册，否则记进 `c6_enum_poison` 并在裸用处报 C119；引用复用 `primary()` 里已有的 `pkg.member` 点号路径——先按带点名字查枚举常量，查不到再按包名 mangle；`decl()` 在条件位置向前看一个记号，`'Tag.'` 直接交回 `gexpr()`，所以 `if (Color.RED)`/`while`/`for` 的条件不会被当声明；诊断 C117–C120，语法见 [grammar.md](../reference/grammar.md) §3.8；`tests/enum_scope.c6`、`tests/errors/enum_scope.c6` |
| 33 源文件后缀 `.c6` | ✅ | `libtcc.c`：`guess_filetype()` 把 `c6` 归入 `AFF_TYPE_C`；`tcc_add_file_internal()` 在 `(flags & AFF_TYPE_MASK) == AFF_TYPE_C` 且文件名后缀是 `.c6` 时临时置 `s1->c6_ext = 1`，`tcc_compile()` 返回后恢复——每个 TU 独立判断，`.c` 文件不会因为同批出现 `.c6` 而被连带开启，native 与内置 C99 后端共用 `libtcc.c`，C99 输出路径同样生效；`tccgen.c` 的 `c6_module_try()` 依次找 `NAME.c6`、`NAME.c`（点号形式同理，`.c6` 优先）；用例 `tests/c6ext/basic.c6`、`tests/c6ext/c6mod.c6` + `tests/tu/c6mod.c6`、`tests/c6ext/pick.c6`（同名 `.c6`/`.c` 并存时选 `.c6`），`tests/run-tests.sh` 的 1b（`-test`）与 1c（`-run`）两组 |
| 34 后缀成为唯一开关（删除 `-std=c6`） | ✅ | `libtcc.c`：`TCC_OPTION_std` 的 `=c6` 分支改成硬报错 `-std=c6 is gone: the '.c6' suffix is the only switch, so drop the option (rename a plain C source to '*.c6' to enable the extensions)`（静默忽略只会让第一处 c6 语法报出莫名的 “identifier expected”）；`c6_ext` 只由 `tcc_add_file_internal()` 按后缀逐 TU 设置，`__C6__` 跟着 TU 走；`'defer'` / `'try'` / `'catch'` / `'foreach'` 一类的 “requires -std=c6” 诊断统一改成 “requires a .c6 source file”；仓库里的 c6 源码全部改成 `.c6`（`tests/`、`examples/`，包括裸机 payload 与 `harness.py`/`runtests.sh`），纯 C 仍是 `.c`（`*_plain.c`、`*.plain.c`、`examples/boot/io.c`、`examples/freestanding_pthread.c`）；`make c6selfcheck` 改成 `cp tcc/tcc.c build/selfhost/c6/selfcheck.c6` 再按 c6 规则编译（语言由顶层文件后缀决定，`#include` 进来的 `.c`/`.h` 跟同一份规则走），检查范围和以前用开关时一致 |
| 35 泛型 `@` 应用 / `|…|` 绑定（阶段 1–3） | ✅ | `tccgen.c`：模板声明与 `@` 应用（`parse_btype()` / `c6_gen_btype` / `c6_gen_param` / `c6_gen_args` / `c6_gen_bind` / `c6_gen_one_arg`）、实例化与 mangle（`c6_gen_instance` / `c6_gen_mangle` / `c6_gen_mangle_name` / `c6_gen_declare` / `c6_gen_flush` / `c6_gen_drop_struct`）、`|T|` 函数/typedef 模板（`c6_gen_fn_apply` / `c6_gen_fn_instance` / `c6_gen_fn_drop`）、体内成员函数 V3（`c6_gen_meth_decl` / `c6_gen_meth_sym` / `c6_gen_method_resolve`）、值形参（`c6_gen_nominal` / `c6_gen_arg_only` / `c6_gen_arg_depth`）与深度告警（`-Wgeneric-depth`，`c6_gen_depth`）；实例名带模块前缀、`-emit-header` 导出；诊断 C103–C116；用例 `tests/generic.c6`、`tests/generic_fn.c6`、`tests/errors/generic.c6`、`tests/abi/generic.c6` + `_plain.c`、`tests/c99/generic.c6`、`tests/c99/generic_fn.c6`、`tests/tu/generic.c6`；设计见 [generics-design.md](../design/generics-design.md) |

`foreach` 的设计取舍（评审结论）：

- **头在可迭代对象前面**（`foreach (T v = a)` / `((T i, T v) = a)`）而不是
  Go 风格的 `for a (i, v)`：`for` 必须保持纯 C；把绑定放在可迭代对象前面，
  表达式里就不必再猜括号后面那串是绑定还是可迭代对象的一部分；
- **迭代器是结构化协议，不做装箱**：只要类型提供 `int has(T *)` 与
  `E next(T *)` 就能迭代，元素类型直接取 `next()` 的声明返回类型，所以
  完全静态、没有运行期辅助函数；`void *next()` 会把元素类型擦掉，明确
  报错（C99）；值形态遍历副本、`T *`/接口形态遍历原对象，两种语义都
  有明确写法；不引入名为 `iterator` 的魔法接口，也不要求 `impl` 声明；
- **`lo:hi` 而不是 `0..n`**（P14）：`0..3` 在词法阶段是两个浮点常量
  （`0.`、`.3`），为它改词法会波及现有代码；`:` 在表达式里本来只出现在
  三目运算符里，而可迭代对象按 `conditional-expression` 解析，三目的
  `:` 已经被吃掉，所以 `lo:hi` 没有歧义；
- **绑定在头里声明**（P15）：`(T v = a)` 让类型写在用它的地方，与解构声明
  `(T a, T b) = f()` 同一套形式，也避免“绑定类型从元素类型隐式来”的意外；
  只有带括号、且 `foreach` 不是作用域里已知的标识符时才按语句解析，
  `int foreach = 1;` / `foreach(x);` 照旧是普通表达式；
- **下标默认只是副本**：循环状态（计数器、切片对、迭代器）全在隐藏槽，
  下标每轮拷一份，写它不影响迭代；
- 隐藏槽复用切片运行期布局，因此 `foreach` 不带运行期辅助函数、不改变
  调用约定；`async` 体里照常可用（帧整体保存/恢复）。

元组的设计取舍（评审结论）：

- **不加关键字**：类型写成 `(T1, T2)`，和现有括号语法共用前缀。解析器在
  `parse_btype()` 的 `case '('` 处做一次**投机 + 回滚**（`c6_tuple_speculate`：
  `begin_macro` 重放 token，`type_decl(TYPE_ABSTRACT|TYPE_DIRECT)` 走一遍，
  成功且右括号前没有名字才算元组），失败就原样退回普通括号。因此
  `(void)printf(…)`、`(int)x`、`(struct S){…}`、`({…})`、`(a, b)` 全部零变化；
- **整组扫描要把 `sizeof(type)` 的 `(` 当成开括号**：`sizeof` / `_Alignof`
  后面跟 `(` 时，`unary()` 拿到的是 `TOK_SOTYPE`（`'('` 的别名）。扫描器只数
  真括号的话，匹配的 `)` 会把深度减到 -1，组永远不闭合，一路吞到 `TOK_EOF`；
  这时回放的 token 已经跨过文件尾，行号来自 token 串里的行标记、文件名却来自
  弹出后的包含者，诊断会报成“源文件:头文件里的行号”。`tests/errors/loc.c6`
  守这条；
- **不上新 ABI**：元组就是 `VT_STRUCT` + 内部 tag `__c6_tuple_N`，参数和返回值
  都走已有的 struct 路径（`gfunc_sret` / `classify_x86_64_arg`），`libtcc1.c`
  一个新符号都没加。`tests/abi/tuple.c6` + `tests/abi/tuple_plain.c` 用元组和
  等价 struct 分别编译两个 TU，链接后互相调用；
- **布局不受 `#pragma pack` 影响**：`off = (off + align - 1) & -align` 在类型
  构造时一次算完并缓存，永不读 `pack_stack_ptr`。这样同一个元组在任何 TU、
  任何 pragma 状态下都是同一个 ABI（切片 `T[:]` 用的是同一条思路）；
- **类型回放写 `(T1, T2)` 而不是 tag 名**：`async`/`spawn` 的跳板要把形参类型在
  另一个合成 token 流里重新声明一遍。回放 tag 名会在当前作用域造出一个
  `sym_scope = 1` 的新 struct（和元组不兼容）；回放 `(T1, T2)` 则命中
  `c6_tuple_get` 的规范化缓存，拿回同一个类型；
- **`!error` 允许聚合返回值**：失败与否只看错误槽（`__tcc_err_flag_peek`），
  返回值只承载成功路径的值，而 C15 又强制它必须被 `try`/`catch` 消费——失败
  路径上那个值永远不可观测，所以 struct/union/元组直接清零即可，原来那条
  “必须能用 `-1` 表示哨兵”的限制只是实现偷懒。`float`/`double` 仍然不行
  （既不是标量哨兵也不是聚合），报 C58；
- **`_` 占位符沿用 `foreach` 的约定**：`_` 在解构模式里不建符号、不生成加载，
  别处仍是普通标识符（`int _ = 5;`、`(int, int) _ = f();` 照常）。全是 `_`
  报 C60——那等于把整个值丢掉，应该直接写 `f();`；
- **解构只做语句形式**：`(int q, int r) = f();` / `(q, r) = f();`，不做
  `:=` 类型推断、不做嵌套解构、不做 `t = {1, 2}` 这类字面量糖（要构造临时值
  就写 `(int, int){1, 2}`）。实现上把元组溢到隐藏局部 `__c6_tuple_tmpN`，
  再按普通赋值逐元素展开，所以目标可以是任意左值（变量、数组元素、成员）。

测试全部放在仓库根的 `tests/` 下，不进入上游的 `tests/tests2/`：c6 的
测试一律用 c6 自己的 `test` 块写（`tests/tests2` 里的 `.expect` 方案要
比对 stdout，改一行输出就得重生成期望文件）。

| 位置 | 内容 |
|---|---|
| `tests/async.c6` | `async`/`await` 的返回值、挂起后局部变量、嵌套/循环里的 `await`、`defer` 跨挂起、`await c6_io`、任务槽回收；`co`/`task` 的句柄、自动回收、首步执行、轮转交替、`co` 在 `async` 体里仍是顶层、`__c6_co_run` 的错误计数 |
| `tests/thread.c6` | `spawn` 起线程、参数按值拷贝、线程里的 defer/错误槽、线程里再 `spawn`、与主线程并行、`__c6_th_join_all`、软关键字当变量名/label/typedef 用、跨 TU 的 `spawn`（`tests/tu/thread.c6`） |
| `tests/errors/spawn.c6` | `spawn`/`co` 的错误路径：函数外使用、`spawn` 一个 `async` 函数、变参、`!error`、函数指针、不支持的签名、`co` 一个普通函数 |
| `tests/defer.c6` | defer 的时机、顺序、`break`/`continue`/`goto`/`return`、递归 |
| `tests/err.c6` | `err` / `errdefer` 与清理顺序、错误槽 |
| `tests/try.c6` | `try` / `catch`，含 `goto` 离开、嵌套传播、`void!error` |
| `tests/test-block.c6` | `test` 块、`-test`、`-test-filter` |
| `tests/macro.c6` | 四种角色（`expr` 当值/`stmt` 当语句/`ident` 给 `##`/`type` 给声明）、`expr` 实参自动加括号、宏体局部变量自动卫生（同名用户变量不被捕获）、`__local` 手动标注、嵌套调用、同作用域多次展开、角色词当普通标识符 |
| `tests/errors/macro.c6` | 宏定义的编译期诊断（C129–C134：形状、形参缺角色、`#`/`##` 用在非 `ident` 形参、`expr` 带语句、`#macro` 迁移、`__local` 拼名字） |
| `tests/module.c6` | `module` / `import`：限定名、带前缀的 C 符号、全限定函数取址、跨 TU |
| `c6ext/modpath`（run-tests.sh） | 模块名即路径：分级查找、扁平拼写被拒绝、两个 TU |
| `tests/method.c6` | 结构体方法、值接收者（`v.f()` = `f(&v)`，改动可见、返回结构体、`!error`、泛型实例上的 `|T|` 方法、与接口 vtable 的分工）、不同类型的同名方法、libc 方法（`p->fflush()`）、与方法或 libc 同名的普通函数不被顶掉，以及 `T!error` 方法 |
| `tests/interface.c6` | 接口、vtable、隐式/显式转换、跨 TU |
| `tests/iface_agg.c6` | 接口方法签名里的聚合类型：struct 参数、struct 返回值、12 字节 struct 返回值（寄存器返回）、元组返回值（`tests/errors/iface.c6` 的 `test_proto_scope_tag` 守住“tag 必须在接口之前声明”的诊断） |
| `tests/slice.c6` | `T[:]` 的构造、下标、区间、跨 TU |
| `tests/typename.c6` | 裸 tag 类型名：声明/成员/形参/返回值/方法接收者/切片元素/强转/`sizeof`/复合字面量/typedef/`const`、自引用链表、`typeof`/`_Generic`/`_Alignof`、接口方法签名、`TAG:` 标签、遮蔽（同名函数、同名局部变量、`struct stat` + `stat()`）、跨 TU（`tests/tu/typename.c6`） |
| `tests/errors/typename.c6` | 裸 tag 的退回路径：被函数/局部变量/POSIX `stat` 遮蔽、tag 未声明、tag 当值用、tag 出作用域、局部 tag 的切片、不完整类型 |
| `tests/enum_scope.c6` | 枚举成员的限定名 `Color.RED`：两个枚举共用成员名、裸名仍可用、常量表达式（数组长度 / `case` / 位移）、`if`/`while`/`for` 条件里的限定名、块作用域枚举 |
| `tests/errors/enum_scope.c6` | 限定名的编译期诊断：限定符不是自己的 tag（C117）、匿名枚举（C118）、裸名被两个枚举抢占（C119）、成员不存在（C120）、限定符后缺名字、都不带限定时仍是重定义 |
| `tests/foreach.c6` | 数组/切片/区间三种可迭代对象、值绑定是副本、可迭代值只求值一次、`_` 丢弃、`break`/`continue`/`defer`/嵌套、绑定不泄漏、`size_t` 下标可写、`await` 跨挂起 |
| `tests/foreach_head.c6` | 声明式头：`(T v = a)` 值绑定是副本、`((T i, T v) = a)` 下标+值、`(T i = lo:hi)` 半开区间、绑定类型可与元素类型不同、写绑定不影响迭代、`_` 丢弃、与新写法等价性、`foreach` 软关键字 |
| `tests/errors/foreach_head.c6` | 声明式头的编译期诊断：缺类型/缺名字、`void`、数组绑定、下标非整数、区间给两个绑定、迭代器给两个绑定、托管所有权改变、文件作用域 |
| `tests/foreach_iter.c6` | `has()`/`next()` 迭代器：值/指针/接口值三种形态、结构体与指针元素、`_`、可迭代值只求值一次、空迭代器、无限迭代器 + `break`、`continue`、`defer`、绑定不泄漏、嵌套、`await` 跨挂起、union、跨 TU、托管元素/迭代器（含临时迭代器）与数组托管元素不泄漏 |
| `tests/errors/foreach_iter.c6` | 迭代器协议的编译期诊断：方法签名（C99）、多绑定（C100）、只有 `has()`/字段遮蔽（C47）、数组元素、不完整元素、托管结构体元素、文件作用域 |
| `tests/fstr.c6` | `f".."`：整数/`_Bool`/`char`、文本（C 字符串/数组/字符切片/托管 `@char`）、指针、花括号与转义、任意表达式、嵌套（1 层与 2 层）、空串与长文本、`@f""` 的生命周期、循环里的 `@f""` 不漏引用、同深度缓冲的覆盖语义、`err(f"..")` 的消息跨 TU 被复制、把结果传给另一个 TU（`tests/tu/fstr.c6`） |
| `tests/errors/fstr.c6` | `f".."` 的全部编译期诊断（`{}` 空表达式、缺 `}`、不能插值的类型、非字符切片、静态初始化器、借用/托管互存） |
| `tests/errors/foreach.c6` | `foreach` 的全部编译期诊断（绑定数量、指针/整数/结构体/VLA/不完整数组、元素是数组、文件作用域、缺花括号） |
| `tests/tuple.c6` | 元组的值语义、`t[k]`、`sizeof`/`_Alignof`、解构声明/赋值（变量、数组元素、成员）、`_` 占位符、元组当参数/成员/数组元素、嵌套与混合元素、复合字面量、跨 TU（`tests/tu/tuple.c6`） |
| `tests/tuple_error.c6` | 元组/struct 作 `!error` 返回类型：成功路径、失败路径、错误槽卫生、连续 `catch` |
| `tests/tuple_async.c6` | 元组的 `async` 返回、`await` + 解构、`_` 丢弃、嵌套 `await` |
| `tests/tuple_thread.c6` | 元组穿过 `spawn`（按值拷贝）、线程里的元组调用 |
| `tests/gc_return.c6` | 返回托管引用的计数归属：新建对象返回、借字段返回、形参原样返回、循环里多次调用之后计数回到基线（`c6_gc_return_hook()` 的回归） |
| `tests/errors/tuple.c6` | 元组的全部编译期诊断（元素类型、下标、解构形态、`return {…}`） |
| `tests/errors/loc.c6` | 诊断位置：`sizeof(type)` / `_Alignof(type)` 整组回滚之后，头文件里的声明仍然报在头文件自己的那一行 |
| `tests/abi/tuple.c6` + `tests/abi/tuple_plain.c` | 元组与等价 struct 的 ABI 互通（两个 TU 分别是 `.c6` 和纯 C，分别编译后链接） |
| `tests/abi/method.c6` + `tests/abi/method_plain.c` | 方法改名的 ABI 互通：纯 C 一侧按原名调用 c6 方法（弱别名）、c6 一侧普通调用纯 C 函数 |
| `tests/tu/*.c6` | 与同名主文件一起编译的第二个翻译单元（`.c6`：c6 代码只能在 .c6 里） |
| `tests/c6ext/basic.c6` | `.c6` 后缀自测：`defer`、切片、`try`/`catch`，**不加任何开关**（后缀自己开启） |
| `tests/c6ext/c6mod.c6` + `tests/tu/c6mod.c6` | `import tu.c6mod;` 从 `.c6` 模块加载声明，第二个 TU 提供代码（构建带 `-I tests`） |
| `tests/c6ext/pick.c6` + `tests/c6ext/pick/m.c6`/`m.c` | import 查找顺序：同名 `.c6` 与 `.c` 并存时选 `.c6`（`PICK == 11`） |
| `tests/errors/*.c6` | 编译期诊断，见下 |

`tests/run-tests.sh`（`make selftest`）先对每个 `tests/*.c6` 跑一次
`-test`，若存在同名 `tests/tu/<name>.c6` 就一起编译成两个 TU；再对
`tests/c6ext/*.c6` 用同样的 `-test` 流程跑一遍，但**不加任何开关**
（后缀自己开启扩展，构建带 `-I tests` 让 import 找到 `tests/tu/<name>.c6`），
并用 `-run` 跑一个 `.c6` 冒烟；`c6ext/rule` 这一组把规则本身钉住：同一段
`defer` 放进 `.c` 必须报 `requires a .c6 source file`、放进 `.c6` 必须通过，
而 `-std=c6` 必须报错并说明「后缀才是开关」（`.c6` 文件只需去掉选项）；之后对
`tests/errors/*.c6` 的每个 `// EXPECT <片段>: <诊断>` 注释跑一次
`-D<片段>`，要求 stderr 含该诊断，并要求不带 `-D` 时该文件能编译运行通过。
诊断测试因此不需要维护行号。

`tests/abi/*.c6` 是 ABI 回归：`.c6` 的那个按 c6 编译，同名 `_plain.c` 是
纯 C（`.c` 后缀，天然没有扩展），两个 `.o` 链接后运行，
比对 `// EXIT <n>` 声明的退出码。这样“元组的 ABI 等于等价 struct”不是
注释里的承诺，而是每次 `make selftest` 都验一遍的事实。

`make test` / `make test-plain` 仍旧跑上游 `tests/tests2` 全量（134 项），
两种构建下都全过——c6 不再往 tests2 里加用例。

已知限制（与设计一致）：

- 运行时状态（错误槽 / 插值构造器 / 协程池 / 线程链表）是**每线程**一份，
  `defer` 的清理状态例外：它在函数帧里（见 [grammar.md](../reference/grammar.md) 8.2），
  通过 `pthread_key`（不是 `__thread`：`-run` 不支持 TLS）取；`spawn` 出去的
  线程有自己的那一份，所以线程里照常写 `defer`/`err`/`co`，但跨线程共享
  数据仍需自己同步；
- `err` 一旦执行就直接返回，没有重抛形式；转发用 `try`（在返回 `T!error`
  的函数里自动传播），就地处理用 `catch`（快照并清空错误槽）；
- `T!error` 调用的返回值必须被 `try` 或 `catch` 消费，否则编译报错；
- `error` 只能作为返回值：形参表里的 `error` / `error*` / `error[]` 一律
  报错（`c6_check_error_param`），错误通道只有返回值 + 错误槽，
  不引入隐藏参数，也不改变调用约定；
- `!error` 的函数返回类型可以是标量、枚举、指针、struct、union、元组：标量在
  失败路径上返回哨兵 `-1`，聚合类型在失败路径上被清零（`c6_zero_local`）。
  因为 C15 强制返回值必须被 `try`/`catch` 消费，失败路径上的值不可观测，所以
  清零不丢信息。`float`/`double`/`long double` 仍不能标注（没有哨兵，也不是
  聚合）；
- 元组按自然对齐布局，`#pragma pack` 对它**无效**：这是有意的，跨 TU 的 ABI
  必须与 pragma 状态无关（和切片 `T[:]` 同一条规则）；
- 元组元素类型必须在**文件作用域**可见（内部 tag 注册在全局作用域），函数里
  定义的 struct 不能当元素；元素之间可能有 padding，所以不提供 `&t[0]` 式的
  连续访问，也不能把元组直接当 `T[:]` 的底层数组；
- 解构只支持**语句**形式，元素个数必须与元组一致；不做嵌套解构、`:=` 类型
  推断、`t = {1, 2}` 字面量糖、运行期下标 `t[i]`；`_` 只在解构模式里有特殊
  含义；
- `await` 一个失败的 `async` 子任务时，子任务的错误槽在恢复父任务帧时会被
  覆盖，父任务无法用 `catch` 观察子任务的错误：子任务要自己就地处理
  （`catch`），这是无栈协程“错误槽随帧保存/恢复”的已知限制；
- 元组返回值也占 `struct NAME_task` 的 `val` 区，元素多的元组会让任务块更
  大（不是错误，只是内存）；帧本身住在任务里，没有上限；
- `interface` 的方法签名支持聚合类型：struct/union/元组作参数和返回值都可以
  （`tests/iface_agg.c6`）。唯一要求是类型在**接口声明处**已经可见：`struct P;`
  这类声明要写在 `interface` 之前。接口的方法列表按 C 的函数原型重放，第一次
  出现在其中的 tag 属于原型作用域（C11 6.2.1p7），和之后文件作用域的
  `struct P` 不是同一个类型——这种情况会直接提示
  `the interface's 'struct P' is a prototype-scope tag (declare 'struct P;' before the interface)`；
- `((int,int){1,2}[0])` 这种“复合字面量外面再套一层括号、下标写在括号里”的
  写法报 `')' expected (got '[')`：下标写在括号外（`((int,int){1,2})[0]`）或
  去掉外层括号即可；
- 元组/模式猜测会在失败时把扫过的 token 用 `begin_macro()` 塞回输入流，
  而猜测期间调用的 `parse_btype()`/`decl()` 自己也可能再塞一层回放；原来
  收尾的 `end_macro()` 会弹错栈顶，漏在外面的那层回放把 token 再发一遍
  （`((a) < b);` 这种“语句以括号组开头”的写法因此报
  `expression expected before '<'`）。现在这些点统一走 `tccpp.c` 的
  `macro_unwind_above()` / `end_macro_under()` / `macro_rewind()`：先把
  猜测期间压上去的回放全部弹掉，再回到自己的那一层；
- 结构体方法必须在调用点之前声明或定义（单趟编译约束）；接收者写成指针
  （`p->f()`）或值（`v.f()`，按 `&v` 传，要求左值）都行，同名字段优先于
  方法，接口值上的 `.` 调用按 vtable 分派；
- 方法符号是 `<结构体>_<方法>`，源码名是弱别名：同一个类型仍然只能有一个
  同名方法，不同类型的同名方法可以直接共存；`area(p)` 这种普通调用只指向
  本 TU 里第一个取到该名字的方法，有歧义时写 `p->area()`。`static` / `inline`、
  `__` 开头的名字、系统头文件里的声明和匿名结构体的
  方法保留原名，因此两个 `static` 或匿名结构体的同名方法仍然要靠改名区分；
  普通名已经被别的函数占用（同名普通函数、libc 声明）时不发别名，这样的方法
  只能写 `p->f()` 调用；
- 纯 C 翻译单元里定义的方法没有改名符号，c6 一侧只能用 `f(p)` 调用它，
  写 `p->f()` 会在链接时缺 `T_f`；
- `interface` 的一致性检查发生在第一次结构体→接口转换点，因此方法也要在转换点
  之前可见；文件作用域的接口初始化器不支持（没有运行时代码可生成），接口→接口
  是普通拷贝；
- `pkg.member` 必须先 `import`（或位于该包内）；包只是词法前缀，没有目录/文件语义，
  多级包名只是把点号换成 `__`；
- 变量初始化检查在**定义点**做（`c6_check_var_initialized`），不做数据流
  分析，因此 `int x; x = 1;` 也会报错；`extern` 声明、形参、结构体/联合体
  成员、`typedef` 不是定义，不受约束；
- 初始化规则有两类豁免：`static` 变量（文件作用域或块作用域）——`static`
  声明本身就是常驻状态的写法，C 保证静态存储期对象清零，写 `= 0` 只是
  噪声，不带初始化器还让它留在 `.bss` 而不是 `.data`；VLA——标准 C 不允许
  `int a[n] = …`，所以 VLA 仍可不带初始化器；
- 这条规则是**按翻译单元**生效的：`.c` 文件（普通 C）零变化
  （plain 构建下 tests2 仍 134 项全过，c6 的自测在 `tests/` 单独跑）；
  给某个 TU 开启扩展后，该 TU 内所有定义都要补初始化器，K&R 形参声明表除外。TCC 会把显式 `= {0}` 的
  全局/`static` 对象放进 `.data`（不写初始化器时在 `.bss`），因此给大型
  静态缓冲区补初始化器会增大输出文件；
- TCC 自己的源码也按这条规则写：`make c6selfcheck` 把 `tcc.c` 拷成 `.c6` 后用 `-DC6_EXT`
  编译 `tcc/tcc.c`（原生目标文件 + C99 后端产物；带 `C6_EXT` 才会连
  `#ifdef C6_EXT` 的扩展实现一起检查）并把编出来的编译器连起来跑，两步
  编译都不许有诊断输出。
  为此 `tcc/tcc.h` 里那批编译器全局量（`tok`、`file`、`int_type` 等）从
  "头文件里的暂定定义"改成"头文件 `extern` 声明 + 各自 `.c` 里带初始化器
  定义一次"（`ST_DATA` 的 `ONE_SOURCE` 分支也跟着去掉了 `static`）；
  c6 的关键字（`err`、`test`、`import` 等）在 tcc 源码里原来的标识符用法
  改成了别的名字；`x86_64-gen.c` 的 `gfunc_prolog` 定义补了 `ST_FUNC`，
  否则原型（`static`）和定义（`extern`）是两个符号，c6 的方法登记会报
  `duplicate method`；
- `-test` 与 `-run` 同时出现时 `-run` 不告警（两者都选择内存执行）；
- 切片 `T[:]` 是**视图**：`{元素指针, 长度}` 两个机器字，不拥有元素，也没有
  `append`/`cap`/动态扩容/深拷贝；`data` 字段是 `T *const`，所以切片头不能被
  重定向，越界检查以外的指针运算请走 `.data`；
- 切片元素的顶层 `const`/`volatile` 被忽略（`const int[:]` 与 `int[:]` 同类型），
  元素是 struct/union/enum 时其 tag 必须在文件作用域声明——函数内的局部 tag
  会随块退出释放，而切片类型要活到 TU 末尾；
- 数组→切片只在运行期上下文隐式发生（赋值/初始化/实参/`return`/强转）；
  静态初始化器（文件作用域或 `static` 局部）没有可执行代码，写
  `int[:] g = arr;` 会报错，改用 `{ arr, N }`。VLA 与不完整数组不能转切片，
  指针也不能直接切（用 `(T[:]){p, n}`）；
- 切片越界是运行时陷阱（SIGSEGV），没有诊断文本：libtcc1 里没有格式化
  输出，与 `catch` 落空时的处理保持一致；
- 元素本身是数组时先用 `typedef`（`typedef int Row[3]; Row[:] rows;`）——数组
  声明符只能写在名字上，C 本身如此。
- `async` 是**无栈**协程：函数体跑在任务自己那块内存上，`await` 处只把错误
  槽存进任务，`return` 回驱动方；恢复时原样取回并跳到恢复点。清理点的注册状态在函数
  帧里，跟着帧走，所以不需要额外保存。因此没有独立栈、没有
  `setjmp`/`ucontext`，也没有上下文切换开销，但**不能**在 `await` 之后
  依赖别的协程帧还活着；
- 帧住在任务里：后端收尾时用 `c6_frame_size_set()` 回填帧大小，运行期据此
  一次 `malloc` 出 `struct NAME_task` + 帧（不再有 64 个任务的固定池，也不再
  挂起时 memcpy），帧寄存器直接指向那块内存，所以 `&local` 跨 `await` 有效；
- 每线程状态（就绪链表 / 错误槽 / 插值构造器）走 `pthread_key`（不是
  `__thread`：`-run` 不支持 TLS）；c67 这类无法把帧交给 body 的后端在编译期
  拒绝 `async`（拷贝模型已删除），`-b` 与 `async` 互斥；
- 挂起点上已压在求值栈里的中间值必须落在**变量**里（帧槽会随帧一起
  恢复）。实践中调用点本来就会把活寄存器先落到帧槽，所以
  `plain() + await f()` 也能用；静态判断不出来时报约束 C33；
- `async` 的签名必须是能从声明写回任务结构的类型：标量、枚举、指针、
  `T[:]`、文件作用域 struct/union。数组（会退化成指针）、VLA、函数指针
  参数都会报 `'async' does not support this signature`；
- `__c6_co_destroy` 目前只回收任务槽（连同未完成的子任务一起），不会
  执行任务里还挂着的 `defer`；需要清理的协程请让它跑到完成；
- `await` 不能出现在 `defer`/`errdefer` 体、`sizeof` 等不求值上下文，
  也不能在非 `async` 函数里用；`c6_io` 只有 `await c6_io(fd, ev)` 一
  种写法；
- `co` / `spawn` / `task` 是**软关键字**：词法阶段仍是标识符，只在语句开头
  且后面紧跟一个标识符时才按关键字处理。所以 `int spawn = 1;`、`spawn = 2;`、
  `x = spawn;`、`spawn(x);`、`typedef int co; co v;`、`spawn:` 标签都照常工作；
  已经被 `typedef` 成类型名的 `co`/`spawn`/`task` 一定按声明解析；
- `co` 是**同步起点**：任务在 `co` 语句处就开始跑，跑到第一个必须等待的
  点才回到下一条语句。`await c6_io` 总要停一轮（唤醒是下一轮 `select()`
  的事，即使 fd 已经就绪）；`await` 子任务不算等待，所以不碰 fd 的
  `co f();` 会在 `co` 语句里跑完，并**当场回收**（没人拿得到句柄，也没人
  观察得到）；只有 park 在 fd 上的任务才留给 `__c6_co_run()`；
- `co`/`task` 的操作数必须是一次 `async` 调用；`spawn` 的操作数必须是一次
  **普通函数**调用（`spawn` 一个 `async` 函数会提示改用 `co`）。三种写法分工：
  `co` = 无栈协程、`task` = 拿句柄、`spawn` = OS 线程；
- `spawn f(a);` 的实参在 `spawn` 语句处求值一次并按值复制到堆上的参数块，之后
  改局部变量不影响线程；被调函数不能是 `async`、不能变参、不能带
  `!error`、不能走函数指针，形参不能是数组/VLA/函数指针——这几种都报
  约束 C38–C40，而不是悄悄退化成直接调用；
- 线程总是由**启动它的线程**回收（`main` 结束时编译器插
  `__c6_th_join_all()`，别的线程在退出时回收自己的子线程）；不提供取消，
  也没有分离线程。跨线程共享数据要自己加锁（和 C 一样）；
- `spawn` 只支持 ELF 目标；PE 目标上还没有线程支持，运行时会 trap；
- `c6_io` 的 fd 超出 `[0, 1024)` 时任务按失败回收（`__c6_co_wait` 返回
  `-1`，`__c6_co_run` 计一次失败），不会永远 park 在 select 里；
- 调度是**协作式**的：没有抢占，不 hook libc，阻塞的 `read`/`printf` 不会
  自动变成让出点；`__c6_co_run()` 对顶层任务做轮转，一次 `select()`
  等所有 park 住的 fd；
- 任务数不设上限（每个任务一块内存，结束即释放）；`tests/async_frame.c6`
  里 200 个任务同时挂起就是这条的回归用例。
- 裸 tag 类型名只在**普通命名空间里没有同名符号**时生效（约束 C41）：
  `struct stat` 与 `stat()` 共存时，`stat(path, &st)` 仍是调用，`stat st;`
  必须写 `struct stat`。这条规则是刻意的——否则 `S = 3;`、`S * 2;`、
  `S(x);` 这些合法 C 会被 tag 抢走；
- 只接受**已经可见**的 tag，不会隐式前置声明：`S *p;` 在 `S` 未声明时
  仍报 `'S' undeclared`；定义处不能省略关键字（`Vec { int x, y; };` 不是
  语法，裸 `TAG { … }` 分不清 struct/union）；
- 语句开头的 `TAG(...)` 在 `TAG` 只是 tag 时按声明解析（与 C 里 typedef 名
  的规则一致），不再落到「隐式函数声明」；被函数遮蔽时不受影响。`TAG:`
  仍然是标签。
- 同一条规则带来两个边角：K&R 定义里形参名恰好与文件作用域 tag 同名时
  （`int f(S) int *S; { … }`），`TAG` 后跟标识符按原型解析而报
  `';' expected`，写 `struct S` 即可；块作用域的 `TAG;` 只是引用外层 tag，
  不会像 `struct TAG;` 那样声明一个新的局部不完整 tag。
- `foreach` 的可迭代对象有四种：完整数组、切片 `T[:]`、整数区间
  `lo:hi`（半开），以及结构化的迭代器协议 `has()`/`next()`（`T` 值、
  `T *`、接口值都可以）。其它类型请暴露一个切片成员，再
  `foreach ((size_t i, T v) = obj.items)`，或补上 `int has(T *); E next(T *);`；
- 头在可迭代对象前面（`foreach (T v = a)`），绑定必须在头里声明类型：
  循环体必须带花括号；省掉花括号、两个绑定都写 `_` 都会报错而不是猜；
  点号区间 `0..count` 不做（词法上是两个浮点常量），统一写 `0:count`；
- 隐藏槽只对“求值一次”负责：数组每轮重新按 `data[i]` 取元素（数组本身
  可能被改），切片用求值时的 `{data, len}` 快照；值绑定是副本，写下标
  绑定等于改循环变量（与 `for` 一致）。

函数字面量的设计取舍（评审结论）：

- **值就是函数指针，不是"可调用对象"**：无捕获的字面量编译成一个
  `static` 函数，表达式本身退化成函数指示符。因此调用约定、C ABI、
  `-emit-header`、`module` 名字改写全都不用动，`qsort(..., int(const void*,
  const void*) { … })` 直接可用。捕获版（V2）不是按值胖指针，而是一个机器字
  的 `@` 托管引用，隐藏对象里放 `{code, env}`：胖指针含托管字段，C68 会禁止
  它按值拷贝/传参/返回，闭包就不可用了。见
  [closure-design.md](../design/closure-design.md)；
- **不做隐式捕获、也不做"捕获了就按引用用"**：记在栈帧里的局部变量在字面量
  被调用时可能早就没了（存进结构体、交给 C 保存之后），按引用捕获等于造一个
  必然悬空的窗口。V1 一律报 C80，把值作为参数传进来；V2 按值捕获进
  `@struct` env；
- **名字只用序号**（`__c6_fn_<n>`，绝不把类型拼进标识符）：类型可能很长
  （`struct P(int, int[:])`），拼进符号名会撑爆标识符长度、也没人看得懂；
  `static` + `__` 前缀又让它自动躲开 `-emit-header` 与 `module` 改名；
- **探测用 token 重放而不是新关键字**：字面量的头（`void(int x)`）和 C 的
  声明符完全同形，`unary()` 与 `block()` 的语句入口各做一次
  "扫签名 + 重放 `type_decl`" 的投机判定，失败就把 token 原样还回去。所以
  `void (*f)(int) = void(int x) { … };`（声明）和 `void(int x) { … }(1);`
  （语句/表达式）都靠同一套规则判出来，不引入任何保留字，`int x = 5;`
  这类普通声明只在第一个 `=` 处停一下扫描。

### 11.9 C99 输出后端（`-c99`）

新增 `tcc/c99-gen.c` + `tcc/c99-link.c`：复用全部前端（含 c6 语法），
把代码生成到 C99 文本而不是机器码。它不再是独立编译器，而是并进普通的
`tcc/c6`：`tcc/Makefile` 把 `tcc.c` 以 `-DTCC_TARGET_C99` 单源再编一份
`tcc-c99.o`，再用
`objcopy --redefine-sym main=c99_main --keep-global-symbol=c99_main`
把其余全局符号本地化，然后链进 `tcc`；`tcc.c` 的 `wants_c99_output()` 在
解析命令行前决定是否转给 `c99_main()`：`-c99` 显式选择，或者 `-o` 以 `.c`
结尾 / 指向已存在目录（或以路径分隔符结尾）时自动选择；`-E`、`-shared`、
`-r`、`-run` 仍走原生路径。C 输出模式下 `-c` 表示“每个翻译单元一个 .c”，
`-test` 生成带测试入口的 C 源码；`-o <dir>` 按 TU 生成
`<dir>/<输入名>.c`，例如 `tcc -c -o out/ a.c b.c` 得到 `out/a.c`、
`out/b.c`。输出名会覆盖输入文件时直接报错，`-shared`/`-r`/`-run` 在 C 模式
下也直接报错，不会写出空的目标文件。驱动为每个 TU 重放一次自己时，
`c99-gen.c` 的静态生成器状态在 `tcc_new()` 里由 `c99_reset()` 清空，
否则第二个 TU 会继承上一个 TU 的函数体和声明。`c99-link.c` 里的兜底报错
发生在 `tcc_output_file()` 里，即 `tcc_compile()` 已经离开全局状态之后，
所以报错前要先 `tcc_enter_state()`，否则 `error1()` 会解引用空的
`tcc_state` 而崩溃。

（两个编译器各自持有静态状态，不能只按 `TCC_TARGET_*` 在运行时切换：
`tccgen.c` 的寄存器组、返回寄存器、参数求值顺序等都在编译期由 target 宏
决定，所以嵌入的是同一份源码按两个 target 编出的两份代码。）

寄存器 -> `__c99_r[8]` 联合体数组；栈帧 -> `__c99_frm.f` 字节数组（用 tcc
自己的局部变量偏移寻址，因此不需要给局部变量取名字）；`async` body 例外：
它的局部量走运行期给的 `__c99_frmp`（帧住在任务里，见 §9.1 of
`gc-design.md`），`__c99_frm.f` 那份 memcpy 只留给普通函数；比较 -> `__c99_cnd`；
数据段 -> 字节数组 + `__c99_init()` 按重定位表打补丁。

只被引用、不在本 TU 定义的外部符号（libc 函数，以及别的 TU 的函数/对象）
单独声明在 `__c99_ext_<n>` 下，再用 `__asm__` 标签绑回真正的链接符号，生成
代码通过 `#define` 用源名字引用它，这份 `#define` 在前导之前 `#undef` 掉。
原因是声明块里的原型只能来自 tcc 的类型模型（`long` 和 `long long` 是同一个
64 位整型，结构体一律是字节块），和 c6 运行时前导里 `#include` 的宿主头文件
（`<stdlib.h>`、`<pthread.h>`）对不上；换名字后两边永远不碰面，`gcc 1.c`
（不加 `-w`、不加任何额外开关）就能直接编译。本 TU 自己定义的函数（含
`<tag>_<name>` 方法别名）不算导入，仍按原名声明。`make c6selfcheck` 会把
c6 模式翻译出来的 `selfcheck.c` 交给宿主编译器编译一遍，钉住这条。

结构体/联合体、返回结构体的函数、间接调用都已经支持。c6 的运行时
（defer/errdefer、err/try/catch、切片、协程、线程、`c6_io`）用一份可移植
C99 + POSIX（pthread/select）的实现重写在 `tcc/c99-rt.inc`，构建时转成
字符串常量嵌入 `c99-gen.c`，每个 c6 输出文件自带这份运行时；在
GCC/Clang 下运行时符号是 weak 的，多个 TU 共用同一份状态，没有 weak 的
编译器退化为每个 TU 一份。`tests/*.c6`、`tests/exit/*.c6`、
`tests/abi/tuple.c6` 和 `examples/*.c6` 均已通过。

端到端回归直接复用 selftest 的整套用例：`BACKEND=c99 ./tests/run-tests.sh`
（即 `make c99test`）把 `tests/*.c6`、`tests/errors/*.c6`、`tests/exit/*.c6`、
`tests/abi/*.c6` 和 `tests/c99/*.c6` 都先用同一个 `tcc/c6`（`-o ...c`）生成
C 源码，再用
`gcc -std=c99 -pedantic -Wall -Wextra -Werror -pthread` 编译并运行，断言与
native 模式相同（退出码、`// OUTPUT` 输出、`EXPECT` 诊断；只在 C 输出后端
才不同的诊断用 `// EXPECT-C99` 声明）。`make selftest` 用同一个 `tcc/c6`
跑同一套用例（`tests/c99/c99test.c6` 用一份源码覆盖普通 C 和 c6 的全部特性）。

变参函数**定义**也支持：`tccdefs.h` 在 `__c99__` 下给出与 SysV x86-64
一致的 `__builtin_va_list`（`gp_offset`/`fp_offset`/`overflow_arg_area`/
`reg_save_area`），`__builtin_va_start`/`__builtin_va_arg` 复用
`x86_64-gen.c` 的 `classify_x86_64_va_arg`，而 C 后端把形参块 memcpy 进
`__c99_frm.f` 之后，再从 `frame_address(0) - 24` 那个位置把宿主编译器的
`va_list` 复制进变参函数的栈帧顶部——这正是 `__builtin_va_start` 会去读的
位置，所以生成代码里的 `va_arg` 与宿主 `vsnprintf` 之类拿到的 `va_list`
是同一份。整型/浮点变参、超过 6+8 个参数溢出到栈上的情形、
`tests/c99/c99test.c6` 里的“把 va_list 转手给宿主函数”都已覆盖。

自举（`make selfhost` / `make c99selfhost`）：前者是 c6 后端编译自己
（`tcc/c6` -> stage1 -> stage2，两次目标文件 `cmp` 相同），后者是 C 输出
路线（`tcc -c99` 出 C -> 宿主 gcc 编出 stage2 -> stage2 编出 stage3，stage3
再编一次目标文件与 stage2 的逐字节相同）。两条路线最后都让新编译器编一个
小程序并运行，因此“生成的后端能跑”是被回归卡住的，不是靠人工抽查。这两条
路线的存在也顺手钉死了几个只有在自举规模下才暴露的 bug：`nocode_wanted`
的语句不能混进活代码、LLONG_MIN 没有 C 字面量、`.bss` 段必须出现在输出里、
符号地址不能在生成时定死（tentative 声明之后定义还会搬位置）、移位的宽度
取**左**操作数的类型（`(unsigned long long)x << 32` 不能被截成 32 位移位）。

限制（均显式报错，不会静默错编译）：computed goto、VLA；非 POSIX 主机上
运行时可编译，但 `async`/`spawn`/`c6_io` 需要移植。
ABI 方面，不超过 8 字节且只含整数/指针成员的结构体按 x86-64 寄存器返回，
9--16 字节且成员全是整数/指针的结构体也按寄存器返回（生成 C 直接按值返回，
宿主编译器会放进 `rax:rdx`，与原生后端一致），两者都可以与普通 C 互操作
（`tests/abi/sret12.c6`）。含 `float`/`double` 成员的结构体、以及大于 16 字节
的结构体返回用隐藏指针（c6 TU 之间一致，大于 16 字节时也与原生 ABI 一致，
9--16 字节且含浮点成员时与普通 C 混用不支持）。

### 11.10 模板实例的作用域边界与弱符号（写标准库时修掉的三处）

实例化一个模板会在**调用方**的作用域里做两件事：把方法的签名解析一遍（为了
拿到符号），把结构体的成员链重新声明一遍（为了 `sizeof` 和字段偏移）。这两
步都借用了当前作用域，于是留下了三个问题，写 `std/` 时全部撞上并修掉：

1. **方法形参名漏到调用方。** `struct Span@T { T at(int i) { … } }` 实例化后，
   调用方写 `int i = 0;` 变成 `redeclaration of 'i'`。修法：`struct_decl()`
   解析成员签名时给一个独立 scope（`local_scope + 1`），解析完
   `sym_pop(&local_stack, c6_meth_lstk, 1)` 把它从符号链上摘掉（`keep = 1`：
   只摘链接、不释放，符号还被 token 表引用着）。
2. **实例的字段名被另一个实例的同名方法抢走。** 同一个 TU 里同时有
   `struct A@T { int cap; }` 和 `struct B@(N) { int cap() { … } }` 时，A 的实例
   体里裸写 `cap` 会解析成文件作用域的函数 `cap`（`nm` 里能看到 `U cap`）。
   修法：`unary()` 解析标识符时，在通用查找之后、使用符号之前判断——如果当前
   正在生成实例的体内方法（`c6_gen_self_tag`）、查到的符号是个**函数**、而
   接收者又有同名**字段**，就用字段（`self->cap`）。局部变量与形参照旧优先。
3. **跨 TU 重复定义。** 一个 TU 只看到声明、另一个 TU 看到定义时，两边都会
   生成同一份实例体，链接报 `link symbol 'X' defined twice`。修法：
   `c6_gen_flush()` 发实例体之前把符号标成 `a.weak = 1`（函数队列与方法队列
   各一处），链接器保留第一份。

回归用例：`tests/generic_scope.c6` + `tests/tu/generic_scope.c6`（5 个断言，
同时验证跨 TU 实例化）。名字解析次序写进 `docs/reference/grammar.md` 的 C123。

### 11.11 标准库 `std/`

写标准库是这一轮扩展的验收项。第一批是 `std/core/types`、`std/core/err`、`std/container/vec`、
`std/container/span`、`std/container/map`、`std/text/str`、`std/text/fmt`、`std/container/range` 八个模块（一个文件一个
module，模块名就是路径），加一个集成示例 `examples/wordcount.c6`
（`make std-demo`）；第二批补了 `std/core/mem`、`std/os/io`、`std/core/math`、`std/os/time`、
`std/text/strbuf`、`std/text/utf8`、`std/core/bits`、`std/core/hash`、`std/os/path`、`std/container/deque`，并给
`str`/`vec`/`fmt` 补了 `to_u64`/`strip_prefix`/`strip_suffix`/`fields`、
`any`/`all`/`find_if`/`swap`、`digits_*` 原语与 `write_hex64`（见 11.12）。第三批
之后 `make stdtest` 是 25 个模块、197 个 `test` 块，集成示例三个：`std-demo`、
`std-tour` 和 `std-kit`（见 11.13）。设计、API、写法约定与编译器层面的已知限制见
[stdlib.md](../stdlib.md)；上面 11.10 的三处修复就是把它写出来时暴露的。

### 11.12 标准库第二批（strbuf / utf8 / bits / hash / path / deque）

第二批的目标不是「再加几个容器」，而是把**零分配的那一层**补齐：路径、哈希、
位运算、UTF-8 都能不碰堆直接用，容器只留真正需要动态内存的地方。

| 模块 | 定位 | 关键决定 |
| --- | --- | --- |
| `std.text.strbuf` | 长度未知的字符串拼接 | 托管引用 + 从 64 起翻倍；数字转换复用 `std.text.fmt` 的 `digits_u32`/`digits_u64`/`digits_hex`，两条路径格式永远一致 |
| `std.text.utf8` | 解码/编码/校验/计数 | 严格模式：拒绝过长编码、代理区、`> U+10FFFF`、截断；坏字节只前进 1 字节，不会「一错到底」；编码先校验码点，失败不写任何内容 |
| `std.core.bits` | 位运算 | `clz/ctz` 对 0 输入给 32/64（不返回未定义值）；旋转对移位量取模；`bit_*` 越界是 no-op |
| `std.core.hash` | FNV-1a 32/64、CRC32、Adler-32 + 流式 `Hasher` | CRC32 逐位算，不建 256 项表（不占数据、不链 zlib）；已知向量钉死在 `test` 块里（`"a"`、`"123456789"`、`"Wikipedia"`） |
| `std.os.path` | POSIX 路径拆装 | 只借 `Str` 这个类型、函数体不依赖 `std.text.str`；**全部不分配**（视图 + 调用方缓冲）；`normalize` 保留 `".."`——词法消 `..` 会穿过符号链接改变语义 |
| `std.container.deque` | 双端队列 | 环形缓冲（`(head + i) % cap`），扩容后按逻辑顺序摊平；`push_front` 若包在数组上就是 O(n)，所以不包装 `Vect` |

`std.text.str` 补的四个 API 都守住「不分配」：`to_u64` 先判 `(U64_MAX - d) / 10` 再乘
（不靠回绕），`strip_prefix`/`strip_suffix` 只给视图，`fields()` 是跳过空字段的
空白分词迭代器。`std.container.vec` 补 `any`/`all`/`find_if`/`swap`（`all` 对空容器给 1，
`find_if` 没命中给 -1，`swap` 越界返回 0 且不动内容）。`std.text.fmt` 把数字转换抽成
共享原语后，`write_u64`/`write_int`/`write_hex` 只剩「先量长度、再搬字符」。

三个新号段按第一批的规矩进编译器内建表（`tcc/tccgen.c` 的
`c6_err_std_slots[]`）：`std.text.strbuf`=0x600d、`std.text.utf8`=0x600e、`std.core.bits`=0x600f、
`std.core.hash`=0x6010、`std.os.path`=0x6011、`std.container.deque`=0x6012。`errbase.def` **不动**：
std 池是编译器常量，`-emit-errbase` 照样跳过 `area == 1`，`tests/run-errbase.sh`
的 C126（std 槽位不许被钉号）继续成立。

写这批时踩到并绕开的编译器限制（都记进了 [stdlib.md](../stdlib.md) 的「已知限制」）：

1. 把 `Buf@(N)->write_hex` 的形参从 `u32` 加宽到 `u64` 会让编译器**挂住**
   （当时 `-c` 无输出直到超时，退出码 124）。这条**现在已经复现不出来**：拿当前
   `std/text/fmt.c6` 的 body、旧版（`2bf18ce`）的 body、加 `(char)` 强转的变体去试，
   都能在 1 秒内编过，`-test` 也全过，所以留着 `write_hex(u32)` 只是不想为改签名
   动 API（`write_hex64` 两个都是 `digits_hex` 的薄包装）。
2. 切片的字段**按设计**只读：切片是 `struct { T *const data; const size_t
   len; }`，`p.len -= 1` 只给一条 `assignment of read-only location` 警告，但
   tcc 对 `const` 只警告、赋值**确实生效**。所以正确的说法不是「不能改」而是
   「不该改」：视图要变就重建 `(char[:]){ p.data, n }`（`std/os/path.c6` 的
   `strip_trailing`）。要让编译器真的拦住，得在 `verify_assign_cast()`
   （`tcc/tccgen.c:5785`）里把「目标是编译器合成的 slice/tuple 描述符」从
   warning 升级成 `tcc_error`。
3. `catch` 体必须以 `return`/`err()`/`goto` 收尾，所以「失败了记一笔继续跑」的
   演示代码要包一层返回 `-1` 的辅助函数（`examples/stdtour.c6` 的
   `append`/`put_int`/`dq_push_back`）。这条是有意的：`catch` 之后走的是**成功
   路径**，fallthrough 会继续往下执行并复用一个和当前表达式无关的值，所以编译器
   宁可报错（真要掉下去还有 `__tcc_catch_fallthrough` 陷阱兜着）。可以按「被守护
   表达式的值不被使用」放宽——`c6_save_operand()` 返回的 `has_value` 就是判据
   （`void!error` 调用给 0）：把 `tcc/tccgen.c:16967` 的检查改成
   `if (has_value && !c6_last_stmt_leaves) tcc_error(...)`，void 场景就允许
   fallthrough，非 void 场景仍然拦住。

当时的验收：`make stdtest`（19 个模块、149 个 `test` 块、0 failure）、`make std-tour`
（`examples/stdtour.c6`，73 条自检全过）、`make std-demo`、`make errcheck` 全绿。

### 11.13 标准库第三批（ascii / num / codec / rand / fs / set）

第三批补的不是容器，而是「程序除了容器还要用的那一层」：字节分类、浮点的
出入口、文本编码、随机数、文件系统元数据、集合。前两批已经把这层的一半做完了
（path/hash/bits/utf8 都是零分配），这一批继续守住同样的约定：能写调用方缓冲
就不分配，放不下就返回 -1 且一个字节都不写。

| 模块 | 定位 | 关键决定 |
| --- | --- | --- |
| `std.text.ascii` | 字节判别、大小写、忽略大小写比较 | 参数收 `int`：`fgetc()` 的 EOF(-1) 和带符号 char 都安全；`c < 0 || c > 127` 一律判「不是」，不读表越界；只认 ASCII，不做 locale |
| `std.text.num` | 浮点格式化（固定 / `%e` / `%g`）与解析 | 不链 libm；`fmt_f64`/`fmt_f64_sci`/`fmt_f64_g` 都是四舍五入（.5 远离零）、放不下不写；`fmt_f64` 限制 `|x| < 1e18`（整数部分超出 u64 就没法固定表示），大数走 `_sci`；解析最多累积 19 位有效数字，明确不是正确舍入；溢出 `RANGE`、下溢给 0 |
| `std.encoding.codec` | Base64 / 十六进制 / 百分号 | 解码先完整校验一遍、第二遍才写，非法输入不留半截；Base64 接受省略填充与 ASCII 空白，拒绝 `% 4 == 1`、`=` 在中间；百分号的 `+` 是显式参数，默认不当空格 |
| `std.core.rand` | xorshift64* 伪随机 | 同一 seed 跨平台同一串数（不依赖 libc `rand()`）、纯值可重入；`rand_below` 用拒绝采样而不是「取模偏置」；种子 0 换成非零常量（全零状态是吸收态）；明确不是密码学随机 |
| `std.os.fs` | stat/lstat、mkdir_p、目录遍历、rename/remove、cwd/chdir | `std.os.io` 管内容、`std.os.fs` 管元数据；`errno` 翻译成 `std.core.err`（`NO_ENT`/`EXIST`/`INVAL`/`IO`）；`dir_next` 用 1/0/-1/-2 区分「取到/结束/出错/名字太长」，名字太长不截断；POSIX-only，freestanding 用不了 |
| `std.container.set` | 哈希集合 `Set@K` | 不复制 Map 的开放寻址/墓碑/扩容逻辑，直接包 `@Map@(K, int)`；`add` 返回 1（新增）/0（已存在），失败 `int!error`；代价是用 Set 的程序要把 `std/container/map.c6` 正文一起编 |

**写这批时踩到并修掉的真 bug：`std/container/map` 扩容会复活墓碑。** `Map::reserve()`
重建表时写的是 `if (!of[i]) continue;`，而 `SLOT_DEAD` 的值是 2（也是「非零」），
于是墓碑连同旧键值被搬进新表：`count` 少一、已删除的键重新出现。Map 自己的
用例没踩到是因为删除之后没有继续插到触发扩容；`Set` 的 add/remove 组合立刻
暴露了它。修法是把条件改成 `of[i] != SLOT_LIVE`，并在 `std/container/map.c6` 补了
「墓碑不会在扩容时复活」用例（删一个键、继续插到 cap 32→64、验证死键不在、
活键都在，再 reuse 那个槽位）。

编译器层面这一批确认了两件事：

1. **嵌套泛型实例当字段是可行的**：`struct Set@K { @Map@(K, int) m; }` 能声明、
   能 `new Map@(K, int)`、能调 `m->put()`，`Iter@(K, int)` 也能当 `SetIter@K`
   的字段。11.12 里「模板体内方法不能返回多参数泛型实例」仍然成立，`Set.iter()`
   因此返回单参数的 `SetIter@K`。
2. **在导入方用实例类型写辅助函数仍会出警告**：在普通 TU 里写
   `static int f(Set@int *s) { return s->has(k); }` 会得到
   `assignment makes integer from pointer`（返回值类型没解析对），程序能跑但
   不该依赖。`examples/stdkit.c6` 的 `tour_set` 因此把逻辑留在局部
   `@Set@int` 变量上，`docs/stdlib.md` 的「写法约定 / 已知限制」记了这一条。

号段继续按第一批的规矩追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：
`std.text.ascii`=0x6013、`std.text.num`=0x6014、`std.encoding.codec`=0x6015、`std.core.rand`=0x6016、
`std.os.fs`=0x6017、`std.container.set`=0x6018，`docs/reference/grammar.md` §8.1 同步。`errbase.def`
仍然不动：std 池是编译器常量，C126（std 槽位不许被钉号）继续成立。

集成示例是 `examples/stdkit.c6`（`make std-kit`，70 条自检），六节各对应一个
模块，并且把 `std/os/fs.c6` 的目录生命周期完整走一遍（mkdir_p → 写文件 → stat →
遍历 → rename → remove/rmdir）。验收：`make stdtest`（25 个模块、197 个
`test` 块、0 failure）、`make std-kit`（70 条自检全过）、`make std-tour`、
`make std-demo`、`make errcheck` 全绿。


### 11.14 标准库第五批（base64 / url / uuid / mime）

第五批补的是**协议与命令行那一层**：编码、URL、标识符、内容类型。它们都有现成的
外部标准（RFC 4648 / 3986 / 9562（原 4122）/ 媒体类型登记），所以这一批的重点不是
设计 API，而是**把标准的边角钉死**——表与填充、`..` 与非正常路径、版本与变体位、
魔数。

| 模块 | 定位 | 关键决定 |
| --- | --- | --- |
| `std.encoding.base64` | 标准表与 URL 安全表 | `std.encoding.codec` 那份是宽容口径；这一层把**表**（`url` 参数）与**填充**（`pad` 参数）变成显式参数，因为 JWT/JWKS 与 HTTP 头的要求不一样。两张表互不混用：URL 变体下 `=` 是**非法字符**（发送方用错表的信号），标准表下 `-`/`_` 也是非法字符。`pad = 1` 时缺填充照样报错 |
| `std.net.url` | RFC 3986 分解、相对解析、百分号编解码 | `remove_dot_segments` 逐条照抄 §5.2.4（不用「按段重组」），§5.4 的 23 条 normal + abnormal 用例全钉住；结果先在栈上拼好、**最后一步才写 dst**，所以「放不下」不留半截；IPv6 字面量里的冒号不算端口分隔符 |
| `std.encoding.uuid` | v4 / v5 / v7 | `Uuid` 是 16 字节纯值；v5 的 SHA-1 在文件内部实现（不链加密库，已知向量与 Python `uuid5` 逐字节一致）；v7 前 48 位是 Unix 毫秒；随机来自时钟 + 栈地址播种的 `std.core.rand`，**明确不是密码学随机** |
| `std.net.mime` | 扩展名 ↔ 类型、分类、魔数嗅探 | 查不到给 `application/octet-stream`（HTTP 要一个能用的值）；反向查表是多对一（`jpg`/`jpeg` 都是 `image/jpeg`），返回第一个命中；`mime_sniff` 只看魔数，纯文本返回空，不假装能猜 |

写这一批时踩到并修掉的三个真问题：

1. **`b64_decode_inplace` 不补 NUL。** 就地解码的输出永远比输入短，尾巴上会留着
   输入里没被覆盖的编码字符——`"Zm9vYmFy"` 就地解码得到 6 个正确字节，后面还跟着
   `Fy`，当字符串用就多出两个字符。修法是在成功时多写一个结尾 NUL（占 cap 里一格），
   并在文档里把「编码一律不写 NUL，只有就地解码写」这条不对称约定写明。
2. **严格填充只查了「填充补齐」，没查「该有的填充」。** `pad = 1` 时 `"Zg"`（少了
   `==`）原本返回 1 字节，意味着被截断或被中继剥掉填充的输入会被静默接受。修法是
   在 `b64_decoded_len` 里补一条：要求填充时，尾部不整块又没 `=` 一律 -1。
3. **全零初始化的 `Url` 会被当成「显式端口 0」。** `Url u = { 0 };` 是 C 的自然写法，
   而 `url_effective_port` 原本用 `port >= 0` 判断「有没有给端口」，于是 0 变成了
   一个合法端口、`http` 的默认 80 被绕过。修法是改判 `port_str` 有没有内容，并补
   一条回归用例（`{ 0 }` 给 -1、显式 `:0` 给 0）。

号段照旧追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.base64`=0x601d、
`std.net.url`=0x601e、`std.encoding.uuid`=0x601f、`std.net.mime`=0x6020，`docs/reference/grammar.md`
§8.1 同步；`errbase.def` 不动，C126 继续成立。

集成示例是 `examples/stdweb.c6`（`make std-web`，58 条自检）：base64 走一遍
「标准编码 → URL 变体 → 严格填充 → 就地解码 → 放不下」，url 走「分解 → 默认端口 →
IPv6 → 查询参数 → 相对解析 → 百分号往返」，uuid 走「v5 已知向量 → v4 → v7 时间序 →
格式化/解析往返」，mime 走「路径 → 分类 → 字符集 → 反查 → 魔数」。验收：`make stdtest`
（33 个模块，0 failure）、`make std-web`（58 条自检全过）、`make errcheck` 全绿。

### 11.15 标准库第六批（inet / bitbuf / pqueue / ini）

第六批补的是**网络与二进制协议的原语**：地址、位流、优先队列、配置文件。它们的
共同点是「标准写得很细、实现很容易在边角上出错」，所以这一批的重点是把边角钉成
自测用例，而不是铺 API 面积。

| 模块 | 定位 | 关键决定 |
| --- | --- | --- |
| `std.net.inet` | IPv4/IPv6、CIDR、端口 | IPv6 输出按 **RFC 5952** 规范化（最长零段压 `::`、同长取第一个、单组零不压、v4-mapped 写 `::ffff:a.b.c.d`），10 组输入/期望全在自测里；解析严格：前导零、两个 `::`、组数不对、内嵌 IPv4 不在末尾都拒。分类函数把 v4-mapped 折回 v4 再判 |
| `std.encoding.bitbuf` | 位级读写器 | **MSB-first**；越界只报 `FULL`/`SHORT` 且不动游标、不写半截；`bw_patch` 在字节对齐处回填 8/16/32 位且**不移动游标**；`bw_put_bytes`/`br_get_bytes` 要求已对齐（半字节读写没有意义） |
| `std.container.pqueue` | 托管二叉堆 `PQueue@T` | 比较器用 qsort 风格函数指针，与 `std.container.sort` 的 `cmp_int`/`cmp_str` 通用；不设比较器时按字节序（同 `sort_lex` 口径）；`push_pop` 做 top-k，`heapify` 重建，`is_valid` 给测试用 |
| `std.data.ini` | INI 零分配读取 | 字段全是指向输入的 `Str` 视图；`#`/`;` 只做**整行注释**（值里可以有 `#`）；`key = value` 取**第一个** `=`；坏行返回 -1 并给准确行号；`ini_lookup` 遇坏行直接放弃，不假装读到后面的键 |

写这一批时踩到并修掉的三个真问题：

1. **`bw_put_bytes`/`br_get_bytes` 用增长中的游标当下标。** 循环里既写
   `dst[bits/8 + i]` 又 `bits += 8`，于是第二字节跳到 `i+3` 的位置。写得对称
   （读侧同样的错），所以「写进去再读出来」的往返用例反而互相抵消、看不出问题——
   直到示例里按位置断言字节才暴露。修法是先算一次 `base = bits/8` 再循环，
   循环结束后一次性加 `n*8`。
2. **模块自己的错误码不能当 `int` 字段用。** `std.encoding.bitbuf` 的 `err` 字段记「第一个
   错误的 id」，最初写成 `w->err = std.encoding.bitbuf.FULL;`，编译报
   `cannot convert 'struct __tcc_error' to 'int'`：`模块.成员` 是带 id 与消息的
   错误值（这就是 `err()` 的入参类型），赋给 int 要显式 `(int)`，比较则可以直接比。
   同一批还确认了两条语言规则：`err()` **必须两个参数**（码 + 消息），重抛没有
   `err()` 形式、要写 `try expr;`。
3. **`heapify` 之后下标算错会被 `is_valid` 立刻抓住。** 自测里每个 push/pop 步骤
   都跑一遍 `is_valid()`，把 `sift_down` 的「左右孩子都看完再交换」钉住——只比
   左孩子是这类实现最常见的错。

号段照旧追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.net.inet`=0x6021、
`std.encoding.bitbuf`=0x6022、`std.container.pqueue`=0x6023、`std.data.ini`=0x6024，
`docs/reference/grammar.md` §8.1 同步；`errbase.def` 不动，C126 继续成立。

集成示例是 `examples/stdnet.c6`（`make std-net`，47 条自检）：inet 走「v4/v6 往返、
RFC 5952 压缩、坏输入、CIDR 归属、清位成网络地址、端口边界」，bitbuf 走「三字段
记录头 → 正文 → 读回、peek 不动游标、FULL 不写半截、回填不改游标」，pqueue 走
「小顶/大顶、堆顶、top-k、空堆」，ini 走「逐条读、按 (section,key) 查、值里的
`=` 与 `#`、坏行行号」。验收：`make stdtest`（36 个模块、0 failure）、
`make std-net`（47 条自检全过）、`make errcheck` 全绿。

### 11.16 标准库分目录（平铺 → 七个领域目录）

37 个模块平铺在 `std/` 下已经不好找：容器、文本、编解码、OS 原语混在一起。
这次按领域分成 `core/`、`text/`、`container/`、`os/`、`encoding/`、`data/`、
`net/` 七个子目录。语言规则是「模块名就是文件的路径」，所以**移动文件就是
改模块名**，没有第二条路：

| 新目录 | 模块（原名去掉目录段） |
| --- | --- |
| `std/core/` | `types` `err` `mem` `math` `bits` `hash` `rand` |
| `std/text/` | `str` `strbuf` `fmt` `utf8` `ascii` `num` |
| `std/container/` | `vec` `span` `map` `set` `deque` `pqueue` `range` `sort` |
| `std/os/` | `io` `fs` `path` `env` `time` |
| `std/encoding/` | `codec` `base64` `sha256` `bitbuf` `uuid` |
| `std/data/` | `json` `csv` `ini` |
| `std/net/` | `inet` `url` `mime` |

消费侧从 `import std.vec;` 变成 `import std.container.vec;`，命名空间前缀
从 `std__vec__` 变成 `std__container__vec__`。三处必须一起改，否则链接期
才会暴露：

1. 每个文件首行的 `module NAME;` 与文件内所有 `import`（含模块自己写头注释
   里的示例）；
2. `tcc/tccgen.c` 的 `c6_err_std_slots[]`：只换名字，**槽位数值不动**——
   `std.core.err` 仍是 0x6000，`std.data.ini` 仍是 0x6024，所以
   `tests/err_ids.c6` 断言的 `0x0e000003` 不用改数值；
3. `Makefile` 的 `stdtest` 从 `ls std/*.c6` 改成 `find std -name '*.c6'`，
   依赖 case 和六个集成示例的源文件清单跟着换路径。

`errbase.def` 不动：std 池是编译器里的常量，`-emit-errbase` 照旧跳过
`area == 1`，C126 继续成立。文档（README 模块表、`docs/stdlib.md` 分组表、
`grammar.md` §8.1 号段表）同步成分组形式；`docs/stdlib.md` 的 stdtest 输出
示例也重新按实际输出生成。验收：`make stdtest`（37 个模块全绿）、六个
`std-*` 集成示例、`make selftest`、`make errcheck`。

### 11.17 标准库方法化（去掉接收者前缀）

`std/` 的普通结构体方法一直写成「第一个参数是结构体指针」的文件作用域函数，但名字
带了接收者前缀：`reader_read_line`、`writer_write`、`arena_alloc`、`random_u64`、
`sha256_update`、`json_get`、`inet_is_private`……文档里写的 `pr->read_line(...)` 实际
编不过，必须写 `reader_read_line(pr, ...)`。这次把方法名统一成裸动词，调用侧一律用
方法语法：

- `Reader`/`Writer`：`ok`/`read_line`/`read_all`/`read_bytes`/`seek_to`/`tell`/`eof`/
  `close`，`write`/`write_line`/`write_bytes`/`write_slice`/`flush`/`close`；
- `Arena`：`room`/`used`/`is_empty`/`reset`/`alloc`/`alloc_array`/`alloc_str`/`alloc_copy`；
- `Rand`：`next_u64`/`next_u32`/`below`/`int_range`/`int_range_incl`/`next_f64`/
  `next_bool`/`fill`/`shuffle`/`shuffle_int`（`u64`/`int`/`f64` 是类型名，不能当方法名）；
- `Hasher`、`StrBuf`、`Sha256`/`Sha256Hex`、`Uuid`、`Json`、`CsvReader`、`IniReader`、
  `Dir`、`CFile`、`BitWriter`/`BitReader`、`Duration`/`Civil`、`Ipv4`/`Ipv6`/`Ip`、
  `Url` 同样去掉了各自的前缀；`BitWriter.align` 与 `BitReader.align`、`close`/`ok`/
  `format`/`next` 这些重名方法按接收者 tag 区分。

能这样重名的原因是符号规则没有变：方法符号是 `<Tag>_<name>`，模块里的裸名弱别名是
`<module>__<name>`，不会进全局命名空间；同一模块里不同结构体的同名方法也不会互相
顶掉。普通 C 文件（不是 module）里的方法仍建议加接收者前缀，因为那里的裸名弱别名
是全局的。`foreach` 的迭代器协议仍然只认字面上的 `has()`/`next()`，所以 `Split`/
`Lines`/`Fields`/`Range`/`StepRange`/`CFile` 的这两个方法名不动。工厂和一次性函数
（`reader_open`/`rand_seed`/`bw_open`/`br_open`/`csv_reader`/`ini_open`/`arena_make`/
`url_parse`/`uuid_parse`）仍是自由函数；泛型模板体内的方法（`Vect.push` 等）也不变。

改动覆盖 37 个 `std/` 模块、六个 `std-*` 集成示例、`docs/stdlib.md` 与 README 的模块表/
示例/写法约定；原来的 `name(&v, ...)` 调用统一改成 `v.name(...)` / `v->name(...)`。
验收：`make stdtest`（37 个模块、0 failure）、`make std-demo`、`make std-tour`、
`make std-kit`、`make std-box`、`make std-web`、`make std-net`、`make selftest`、
`make errcheck`。

### 11.18 标准库第七批（bitset / glob / base32 / http）

第七批补的是**底层容器原语、模式匹配和两种文本协议**，并且按 11.17 写下的硬性要求，
所有能表达成方法的 API 都写成方法：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.container.bitset` | 借用调用方 `u64` 缓冲的位集合 `Bitset` | 纯值视图，不分配；写越界给 `INVAL`、读越界给 0/-1；最后一个字超出 `size()` 的位始终清零，所以 `count`/`equals`/集合运算不用再掩码；集合运算要求两边位数相同，失败不改接收者 |
| `std.text.glob` | `Glob` 的 `match`/`match_path`/`has_magic`，`glob` 工厂 | `*`/`?`/`[a-z]`/`[!…]`/`\` 转义；`match_path` 的 `*`/`?` 不跨 `/`；坏字符类退回字面量 `[`，不报错 |
| `std.encoding.base32` | RFC 4648 标准表与 base32hex | 和 base64 同口径：不分配、写调用方缓冲、放不下不写半截；`pad=1` 严格要求填充，`pad=0` 编码不补、解码两种都收；解码跳过空白、接受小写、拒绝非零填充位 |
| `std.net.http` | HTTP/1.x 请求/响应零分配解析 | `HttpMessage` 的起始行/头字段/body 都是输入上的视图；头字段名大小写不敏感，值 trim SP/HTAB，不接受 obs-fold；`content_length` 只做语法解析，不截断 body |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.container.bitset`=0x6025、
`std.text.glob`=0x6026、`std.encoding.base32`=0x6027、`std.net.http`=0x6028；`grammar.md`
§8.1 的表同步。`Makefile` 增加 `std-more`，`STDMORE_SRCS` 显式列出模块正文，`stdtest`
给 bitset 补上 `std/core/bits.c6` 依赖，`test-all`/`check-all` 都带上它。

验收：`make stdtest`（41 个模块、0 failure）、`make std-more`（28 条自检全过）、
`make check-all`。

### 11.19 标准库第八批（base58 / varint / semver / cookie）

第八批补的是**大数编码、变长整数和两个协议/版本辅助模块**，继续按 11.17 的硬性要求
用结构体方法：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.base58` | Bitcoin 字母表的 Base58 | 前导零字节写 `1`；编码把输入当大端整数反复除 58；`b58_encoded_len` 明确是上界（精确长度要真做一遍大数除法），解码按 `b58_max_decoded_len` 给缓冲；放不下不写半截 |
| `std.encoding.varint` | LEB128 无符号 / 有符号（zigzag） | 无符号最多 10 字节；解码返回 1/0/-1（成功/截断/非法或溢出），只有成功才更新游标与输出；zigzag 让小的负数也短 |
| `std.text.semver` | 语义化版本 2.0.0 | 严格前导零与标识字符校验；预发布标识逐个比较（数字按长度、数字 < 非数字），构建标识不参与优先级；`format` 先算总长再写，放不下不写半截 |
| `std.net.cookie` | Cookie / Set-Cookie 零分配解析 | 第一段 `name=value` 与 `;` 分隔的属性都是输入上的视图；属性名大小写不敏感，`Secure`/`HttpOnly` 无值旗标也算存在；不处理引号里的分号（明确限定） |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.base58`=0x6029、
`std.encoding.varint`=0x602a、`std.text.semver`=0x602b、`std.net.cookie`=0x602c；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-next` 与 `STDNEXT_SRCS`，
`test-all`/`check-all` 都带上它。

验收：`make stdtest`（45 个模块、0 failure）、`make std-next`（23 条自检全过）、
`make check-all`。

### 11.20 标准库第九批（list / ring / utf16 / ansi）

第九批补的是**侵入式/固定容量容器和两种文本层工具**：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.container.list` | 侵入式双向链表 `List`/`ListNode` | 不拥有节点、不分配；push/pop/remove 只改指针；`remove` 先 O(n) 确认节点在本链表，避免误删别的链；`clear` 摘下节点但不碰 data；`ListIter` 提供 has/next |
| `std.container.ring` | 借用调用方数组的固定容量环形队列 | 纯值视图，不分配；满时 push 返回 0 且不覆盖；pop/peek/at 用输出参数返回 void*，元素为 0 也能和空区分 |
| `std.text.utf16` | UTF-16 解码/编码/计数 | 代理对解码成一个码点；落单代理/坏代理对给 -1，代理对中间截断给 0；编码只收 <= U+10FFFF 且不在代理区的码点；不做 BOM/字节序转换 |
| `std.text.ansi` | ANSI 转义序列构造器 `Ansi` | 只拼字节、不写 stdout；借用调用方缓冲，满了置 ok=0 并忽略后续写入；SGR/8 色/256 色/清屏/光标移动；cstr() 末尾补 NUL |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.container.list`=0x602d、
`std.container.ring`=0x602e、`std.text.utf16`=0x602f、`std.text.ansi`=0x6030；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-node` 与 `STDNODE_SRCS`，
`test-all`/`check-all` 都带上它。

验收：`make stdtest`（49 个模块、0 failure）、`make std-node`（17 条自检全过）、
`make check-all`。

### 11.21 标准库第十批（props / multipart / punycode / width）

第十批补的是**配置、表单、IDN 与终端排版**这一条链路上的四块：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.props` | `.properties` / `.env` 零分配读取 | 值视图保留转义原文，`props_decode` / `e.decode` 才解；续行让 value 跨过 `\`+换行；未知转义丢反斜杠、坏 `\u` 给 -1；空键是在 `r.err` 里给行号 |
| `std.net.multipart` | `multipart/form-data` 零分配解析 | boundary 从 Content-Type 里取（引号里的 `;` 不切）；分隔符必须在行首且后面只跟 CRLF/LF 或 `--`，伪分隔符当 preamble；无头 part 也认；正文不含分隔符前的 CRLF |
| `std.encoding.punycode` | RFC 3492 Punycode + IDNA `xn--` | Bootstring 与 A-label 两层分开；纯 ASCII 也带结尾 `-`；`*_len` 给精确长度；不做 Unicode 归一化/大小写折叠；解码做溢出与代理区检查 |
| `std.text.width` | 终端显示宽度（Unicode 15.0 表） | W/F 2 列、Mn/Me/Cf 与谚文中/终声 0 列、Ambiguous 1 列；不做字素簇合并；裁剪只在码点边界切，`text_width` 从不失败 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.props`=0x6031、
`std.net.multipart`=0x6032、`std.encoding.punycode`=0x6033、`std.text.width`=0x6034；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-form` 与 `STDFORM_SRCS`，
`test-all`/`check-all` 都带上它。

验收：`make stdtest`（53 个模块、0 failure）、`make std-form`（26 条自检全过）、
`make check-all`。

### 11.22 标准库第十一批（crc / fixed / pool / frame）

第十一批补的是**无 FPU、无堆的嵌入式基础**——四块只依赖 `std.core.types`、不分配、
不链 libm 的零依赖模块：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.crc` | 嵌入式校验：CRC-8（SMBUS/1-Wire）、CRC-16（CCITT-FALSE/XMODEM/MODBUS/ARC）、CRC-32C | 全部逐位算、不建表（256 项的表在小 MCU 上就是 512 字节 .rodata）；`*_update` 是流式原语，poly 写正规形式、`refin` 控制反射，init/xorout 归调用方；另带 Fletcher-16/LRC/XOR8/sum8/sum16 |
| `std.core.fixed` | Q16.16 定点 `Fix` 与 Q15 `Q15` | 四则饱和（溢出钳到 I32_MAX）、开方与 Taylor sin/cos（误差 < 1e-5）、Q15 饱和乘；不链 libm |
| `std.core.pool` | 借用调用方缓冲的定长块池 `Pool` | O(1) alloc/free、块与基址 8/16 字节对齐、拒绝池外/不对齐指针；LIFO 复用；不碰堆 |
| `std.encoding.frame` | COBS 与 SLIP（RFC 1055）分帧/转义 | 先量再写（`*_encoded_len`/`*_max_len`），坏 code 字节、截断/非法转义、负载里的分隔符都拒绝，失败时不写半截 |

错误号段追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.crc`=0x6035、
`std.core.fixed`=0x6036、`std.core.pool`=0x6037、`std.encoding.frame`=0x6038。
`Makefile` 增加 `std-embed` 与 `STDEMBED_SRCS`，`test-all`/`check-all` 都带上它。

验收：`make stdtest`（57 个模块、0 failure）、`make std-embed`（30 条自检全过）、
`make check-all`。

### 11.23 `!error` 支持浮点返回类型

C9 原先在声明处拒绝 `float` / `double` / `long double`：失败路径要么用 `-1`
哨兵（浮点的 `-1` 位型是 NaN），要么按聚合清零，两者都不适用。现在把浮点并进
聚合那条路：`c6_emit_propagate` 与 `c6_async_propagate` 对 `is_float()` 的返回
类型改为清零局部槽再返回（全零位型 = `+0.0`），C99 后端与 native 后端共用同一
套 vstack 操作。整数类标量仍是 `-1` 哨兵；数组仍被 C9 拒绝（不是 C 的返回类型），
诊断用例改成数组。失败值本身仍不可观测（C15 强制消费），所以这只影响“编译器
能不能构造出失败值”，不改变错误槽语义。

验收：`tests/run-tests.sh try`（22 条）、`tests/run-tests.sh errors/try`
（17 个片段）、`make check-all`。

### 11.24 标准库第十二批（regex / diff / fuzzy / wrap）

第十二批补的是**文本算法**这一条链路：匹配（regex）、比较（fuzzy）、排版（wrap）、
版本对比（diff）。四个模块都是零分配或「工作区由调用方给」的写法：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.text.regex` | 回溯式正则：字面量、`.`、字符类、`* + ? {m,n}`（含懒惰）、捕获/非捕获组、分支、锚点、`\d\w\s\b` | 按码点匹配、位置给字节（正好切 `char[:]`）；固定容量的 AST（64 节点/8 类/9 组）在编译期报错而不是越界；单字符量词与「定长链 + 一层捕获组」走迭代快路径（`.*` 在 200KB 上是线性的），分支/嵌套组才是经典回溯，深度上限 4096 防爆栈 |
| `std.text.diff` | 行级差异 + `diff -u` 同形 hunk | Hirschberg 线性空间 LCS（O(na*nb) 时间、O(nb) 空间），先剥公共前后缀；工作区从 `Arena` 分、不 malloc；先量再写，工作区不够（`DIFF_ENOMEM`）或输出放不下时不写半截 |
| `std.text.fuzzy` | 编辑距离/OSA/相似度/子序列打分/`fuzzy_best` | 按码点（`中文` 对 `中问` 是 1）；`lev_distance_max` 用长度差先挡一道；`FUZZY_MAX`=256 码点，超了给 -1 而不是悄悄截断；子序列打分连续 +4、词首 +6，给补全用 |
| `std.text.wrap` | 按显示列折行、整段缩进与居中 | 宽度用 `std.text.width` 的 `cp_width`（CJK 2 列、组合符 0 列）；CJK 之间可直接断行，超长词按码点硬切；`count`/`lines` 与 `write` 共用渲染所以长度精确；不做字素簇合并（明确写进文档） |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.text.regex`=0x6039、
`std.text.diff`=0x603a、`std.text.fuzzy`=0x603b、`std.text.wrap`=0x603c。
`grammar.md` §8.1 的表同步，并补上第十一批漏更新的 0x6035–0x6038。`Makefile`
增加 `std-text` 与 `STDTEXT_SRCS`，`stdtest` 给四个模块补依赖（regex/fuzzy →
`std/text/utf8.c6`，wrap → `std/text/width.c6 std/text/utf8.c6`，diff →
`std/core/mem.c6`），`test-all`/`check-all` 都带上它。

验收：`make stdtest`（61 个模块、0 failure）、`make std-text`（35 条自检全过）、
`make check-all`。

### 11.25 标准库第十三批（bignum / decimal / rational / complex）

第十三批补的是**数值表示**这一层：任意精度整数、十进制金额、精确分数、复数。
四个模块都只依赖 `std.core.types`（complex 另外用 `std.core.math` 的 `sqrt`），
不分配、不链 libm：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.bignum` | 任意精度整数 `Big`（1024 位 = 32 个 32 位 limb） | 值类型、零分配；加减乘、二进制长除法（截断除法语义同 C 的 `/`、`%`）、u32 指数的快速幂、2..36 进制互转；超 1024 位/除零/解析失败都进 `err` 并随结果传播；格式化先写临时缓冲，放不下不写半截 |
| `std.core.decimal` | 十进制定点 `Dec`（i64、单位 1e-9，范围约 ±9.22e9） | **乘除用自带的 128 位小工具**（`u128_mul`/`u128_div`，两个 64 位半宽拆开做）再除 1e9、按 half-up 收尾，所以 0.1+0.2 精确等于 0.3；5 种舍入模式、`format` 输出恰好 N 位小数；与 Python `decimal` 做了 120 组随机交叉验证 |
| `std.core.rational` | 精确分数 `Rat`（i64 分子/分母，始终最简） | 加减乘除先约分再乘；`cmp` 用**连分数比较**（交叉相乘会溢出的两个数也能比，不经过 double）；加减的中间分子放不进 i64 时报 `err`（极端大数请用 bignum/decimal，文档写明） |
| `std.core.complex` | 复数 `Complex`（f64 实部/虚部） | 四则 + 共轭 + 模/模方 + 归一化 + `pow_int`（快速幂、负指数取倒数，含 I32_MIN）；`abs` 用 math 的牛顿 sqrt；除零按 IEEE 给 inf/nan；没有 arg/exp/log/sin（缺 atan2/exp，文档写明） |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.bignum`=0x603d、
`std.core.decimal`=0x603e、`std.core.rational`=0x603f、`std.core.complex`=0x6040；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-num` 与 `STDNUM_SRCS`，`stdtest`
给 complex 补 `std/core/math.c6` 依赖，`test-all`/`check-all` 都带上它。

验收：`make stdtest`（65 个模块、0 failure）、`make std-num`（21 条自检全过）、
`make check-all`。

### 11.26 标准库第十四批（lz4 / rle / base85 / hexdump）

第十四批补的是**压缩与文本化**这一层：把二进制存下来、传出去。四个模块都只依赖
`std.core.types`，不分配、不链 libc 的额外东西：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.lz4` | LZ4 块压缩/解压（`lz4_compress`/`lz4_decompress`/`lz4_compress_bound`/`lz4_table_size`） | 零分配：哈希表（1024 个 int）与输出缓冲都由调用方给；兼容标准 LZ4 块格式（手写标准向量 + 独立 Python 参考实现双向交叉验证）；遵守结尾 5 字节字面量/最后匹配不早于结尾前 12 字节；解码对偏移 0、越界偏移、截断的扩展长度全部拒绝 |
| `std.encoding.rle` | PackBits 游程（`rle_encode`/`rle_decode`/`rle_bound`/`rle_encoded_len`/`rle_decoded_len`）与一阶差分（`delta_encode`/`delta_decode`） | 控制字节语义按 Apple/TIFF（0..127 字面量、129..255 重复、128 no-op、单段 ≤128）；差分模 256，和 RLE/LZ4 组合用；先量再写，坏数据/放不下返回 -1 |
| `std.encoding.base85` | Ascii85（Adobe）与 Z85（ZeroMQ）：`a85_*` / `z85_*` 的 encode/decode/len/valid | 4→5 编码；Ascii85 有 `z` 简写、部分组 n+1 字符、跳过空白与 `~>` 终止；Z85 长度必须 4/5 的倍数、严格字母表；解码对溢出 32 位、非法字符、单字符余数都拒绝；与 Python `base64.a85encode` 做了随机交叉验证 |
| `std.encoding.hexdump` | hexdump 文本生成与反向解析：`hex_opts`/`hexdump`/`hexdump_len`/`hexundump` | 固定布局（8 位偏移 + 分组空格 + ASCII 列），width 1..64、大小写、偏移基数可配；`hexundump` 只把「8 个十六进制数字 + 空白」当偏移（纯数据行不会被误吞），遇到 `\|` 跳过 ASCII 列 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.lz4`=0x6041、
`std.encoding.rle`=0x6042、`std.encoding.base85`=0x6043、`std.encoding.hexdump`=0x6044；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-pack` 与 `STDPACK_SRCS`（四个模块
都只依赖 types，stdtest 不需要额外依赖），`test-all`/`check-all` 都带上它。

验收：`make stdtest`（69 个模块、0 failure）、`make std-pack`（24 条自检全过）、
`make check-all`。

### 11.27 标准库第十五批（toml / xml / query / template）

第十五批补的是**配置与文档格式**这一层：从人写的文本里取值，再把人要看的内容拼回去。
四个模块都是零分配（字段是输入上的视图），只依赖 `std.text.str`（toml 另用
`std.text.num` 的浮点解析）：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.toml` | TOML 子集的事件式读取（`toml_open`/`TomlReader.next`/`TomlEntry.decode`/`toml_int`/`toml_float`/`toml_bool`/`toml_array_len`/`toml_array_at`） | 表与表数组、注释、基本/字面量字符串（含 `\uXXXX`）、十进制/0x/0o/0b 整数、浮点（委托 `std.text.num`）、布尔、可跨行数组；语法错误给行号且坏行会被跳过（能继续读）；不支持多行字符串/内联表类型化/点号键/日期时间语义（文档写明） |
| `std.data.xml` | XML 扫描器（`xml_open`/`XmlReader.next`/`XmlEvent` 的 `attr_count`/`attr_at`/`attr`/`xml_unescape`） | START/END/SELF/TEXT 四种事件；跳过注释/PI/DOCTYPE，CDATA 当文本；属性按需解析（值保留原文，实体用 `xml_unescape` 解）；**做**32 层标签配对校验并用字节偏移报错；不做命名空间/DTD/编码转换 |
| `std.data.query` | `application/x-www-form-urlencoded`（`query_open`/`next`/`query_decode`/`query_encode`/`query_build`/`query_get`/`query_get_nth`/`query_has`） | `&` 分隔、只按第一个 `=` 切、`+` 表示空格（和 `std.net.url` 的百分号语义分开）；解析零分配、名字/值保留原文；编码按 unreserved 原样、空格写 `+`、其它写大写 `%XX`；构建先量再写 |
| `std.text.template` | `{{name}}` 模板（`tpl`/`Template.render`/`render_len`/`render_opt`/`vars`） | 名字两侧空白忽略、`{{{{` 输出字面量 `{{`、值不递归展开不转义；缺变量严格 -1、宽松给空串；`vars` 用「扫描前文」去重（不依赖额外存储）；先量再写 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.toml`=0x6045、
`std.data.xml`=0x6046、`std.data.query`=0x6047、`std.text.template`=0x6048；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-conf` 与 `STDCONF_SRCS`，`stdtest`
给 toml 补 `str/num/err` 依赖、给 xml/query/template 补 `str`，`test-all`/`check-all`
都带上它。

验收：`make stdtest`（73 个模块、0 failure）、`make std-conf`（19 条自检全过）、
`make check-all`。

### 11.28 标准库第十六批（args / table / natural / proc）

第十六批补的是**命令行工具的基础件**：解析参数、把结果排成表格、把数字写成人看的
样子、跑子进程。前三个是纯计算（零分配），proc 是一层 POSIX 薄封装：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.os.args` | 命令行选项解析（`args_open`/`Args.next`/`ArgSpec`/`Arg`/`args_find`/`positional_count`） | 按规格表决定谁吃值（`--name=value`/`--name value`/`-n value`/`-n=value`）；`--` 之后全按位置参数；合并短选项不认；未知选项/开关带值/缺值分别报错；选项与位置参数可任意穿插 |
| `std.text.table` | 按显示列宽对齐的表格（`table_make`/`Table.render`/`table_len`） | 列宽用 `std.text.width`（CJK 2 列）；`border`/`sep`/`pad` 可关；`TBL_LEFT`/`TBL_RIGHT`/`TBL_CENTER`；无表头/无边框/紧贴都支持；先量再写 |
| `std.text.natural` | 自然排序与人类可读化（`natural_cmp`/`humanize_bytes`/`parse_bytes`/`humanize_duration`/`parse_duration`/`humanize_int`） | 数字段按整数比、前导 0 视为相等、超长数字段不溢出；字节数 1024 进制一位小数并可反解；时长最大单位优先、裸数字按秒；千分位含 I64_MIN；都先量再写 |
| `std.os.proc` | 子进程（`proc_run`/`proc_spawn`/`proc_wait`/`proc_run_sh`/`proc_which` + `ProcStatus`） | fork+execvp+waitpid（EINTR 重试）；`ProcStatus` 拆出正常退出/信号；命令找不到按 shell 惯例 `_exit(127)`；`proc_run_sh` 命令串 4KB 栈缓冲；`proc_which` 支持带 `/` 直接查与 PATH 逐段试 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.os.args`=0x6049、
`std.text.table`=0x604a、`std.text.natural`=0x604b、`std.os.proc`=0x604c；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-cli` 与 `STDCLI_SRCS`，`stdtest`
给 args 补 `str` 依赖、给 table 补 `width+utf8`，`test-all`/`check-all` 都带上它。

验收：`make stdtest`（77 个模块、0 failure）、`make std-cli`（17 条自检全过）、
`make check-all`。

### 11.29 标准库第十七批（sha1 / ws / dns / tcp）

第十七批补的是**网络协议基础**：摘要、握手/分帧、DNS 报文、TCP 套接字。前三块是
纯字节处理（零分配），tcp 是一层 POSIX 薄封装：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.sha1` | SHA-1 与 HMAC-SHA1（流式/一次性/hex/常数时间比较） | 零分配、不链加密库；RFC 2202 的 HMAC 向量与 100 万个 a 的官方向量都钉在 test 里；文档明确「只用于协议互操作，抗碰撞用 sha256」 |
| `std.net.ws` | WebSocket 握手与帧（RFC 6455） | `ws_accept` = base64(SHA1(key+GUID))；帧支持 7/16/64 位长度与客户端掩码（u32 掩码键、大端）；`ws_parse` 区分「还不够」与「坏帧」；不做分片重组（FIN 原样给出） |
| `std.net.dns` | DNS 报文的解析与查询构建（RFC 1035） | 压缩指针只允许向前指、最多 64 跳（防环）；名字解码写调用方缓冲并限制 255 字节；`*next` 顺序遍历；A/AAAA/CNAME 取值；不做 EDNS0/DoT/名字转义 |
| `std.net.tcp` | TCP 套接字（监听/连接/接受/收发/超时/半关闭） | getaddrinfo 解析主机（IPv4/IPv6）；port 0 让内核挑；带超时 connect 用非阻塞 + `poll`；方法名避开 libc 的 `send`/`recv`/`close`/`accept`（用 `write_bytes`/`read_bytes`/`close_socket`/`accept_client`）；不做 TLS |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.sha1`=0x604d、
`std.net.ws`=0x604e、`std.net.dns`=0x604f、`std.net.tcp`=0x6050；`grammar.md` §8.1
的表同步。`Makefile` 增加 `std-wire` 与 `STDWIRE_SRCS`，`stdtest` 给 ws 补
`sha1+base64+err` 依赖，`test-all`/`check-all` 都带上它。

验收：`make stdtest`（81 个模块、0 failure）、`make std-wire`（19 条自检全过）、
`make check-all`。

### 11.30 标准库第十八批（graph / lru / trie / region）

第十八批补的是**数据结构与算法**：图、LRU、前缀树、区间集合。四个模块都只依赖
`std.core.types`，所有数组/节点池都由调用方给，模块本身不分配：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.container.graph` | 邻接表图的遍历与最短路（`graph_init`/`add_edge`/`bfs`/`dfs`/`toposort`/`components`/`dijkstra`） | 前向星表示（`head`/`next`/`to`/`w` 全由调用方给），`add_edge` O(1)；BFS 无权最短路、DFS 用显式栈（深图不爆栈）、Kahn 拓扑排序有环返回 0、并查集弱连通分量、非负权 Dijkstra + `prev` 回溯链；队列/栈放不下返回 -1 |
| `std.container.lru` | LRU 缓存（链式哈希表 + 双向链表） | `get`/`put`/`remove` O(1)，`peek` 不动顺序；`put` 满了淘汰尾部；`hits`/`misses`/`evicts` 统计；初始化校验 `htsize` 是 2 的幂且 ≥ 2*cap；未初始化对象的所有入口都安全返回错误 |
| `std.container.trie` | 字节前缀树（调用方节点池） | 节点是「字节 + 兄弟链」，键可为任意字节序列（含 `\0`）；`put` 池满返回 -1 且不写坏结构；`remove` 只清词尾标记（共享节点不回收）；`has_prefix` 判前缀存在 |
| `std.container.region` | 半开区间集合（`add`/`contains`/`overlaps`/`find_gap`/`total`） | 始终保持有序、互不重叠；`add` 合并重叠与相邻、失败不改内容；`contains` 二分；`find_gap` 带 `from + len` 溢出保护；i64 坐标允许负数 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.container.graph`=0x6051、
`std.container.lru`=0x6052、`std.container.trie`=0x6053、`std.container.region`=0x6054；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-algo` 与 `STDALGO_SRCS`（四个模块
都只依赖 types，stdtest 不需要额外依赖），`test-all`/`check-all` 都带上它。

验收：`make stdtest`（85 个模块、0 failure）、`make std-algo`（26 条自检全过）、
`make check-all`。

### 11.31 标准库第十九批（atomic / sync / thread / chan）

第十九批补的是**并发与同步**：语言层面的 `spawn` 早就能起真 OS 线程，但库里
一直没有原子操作、锁和跨线程通信。四个模块分两层——atomic 走编译器的
`__atomic_*` 内建（零依赖、不碰 pthread），sync 是 pthread 的值类型薄封装，
thread 在 sync 之上给出线程池，chan 又在 sync 之上给出通道：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.atomic` | 原子读写与读改写（`Atomic`/`Atomic32` + `SpinLock`） | 字段是普通 i64/int，只通过方法访问；默认 seq_cst，另给 `load_relaxed`/`store_release` 发布口；`cas` 收 expected 指针并在失败时回写当前值；屏障用一个 seq_cst 的 RMW 表达（tcc 还没有 `__atomic_thread_fence`）；64 位原子在 32 位目标上可能落到 libatomic 的辅助函数，这类目标用 `Atomic32` |
| `std.os.sync` | Mutex / RWLock / Cond / Once / Semaphore / Barrier | 类型是值（`{ 0 }` 就是 Linux 上的静态初始化器）；`init`/`destroy` 按接收者重载；`try_*` 不阻塞；限时等待统一 1（等到）/0（超时）/-1（出错）；`Cond.wait` 返回时重新持有锁，条件必须用 `while` 再查一遍 |
| `std.os.thread` | 线程身份/CPU 数/让步/改名 + `ThreadPool` | 池里 worker 用 `pthread_create` 起裸线程（不走 `spawn` 的回收链，`stop` 自己 join）；任务队列由调用方给，满时 `submit` 背压、`try_submit` 不阻塞；`wait` 只保证「现在闲」，之后还能继续投 |
| `std.container.chan` | 有界通道 | 元素按字节块拷贝（`esize`），所以 int/指针/结构体都能过；阻塞收发 + `try_*`/限时；`close_chan` 幂等，关闭后 sender 立刻失败、receiver 取完缓冲里剩下的再失败 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.atomic`=0x6055、
`std.os.sync`=0x6056、`std.os.thread`=0x6057、`std.container.chan`=0x6058；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-thread` 与 `STDTHREAD_SRCS`，
`stdtest` 给 chan 补 `sync` 依赖，`test-all`/`check-all` 都带上它。

验收：`make stdtest`（89 个模块、0 failure）、`make std-thread`（29 条自检全过）、
`make check-all`。

### 11.32 标准库第二十批（tty / signal / pipe / poll）

第二十批补的是**终端与进程控制**：前面几批有了进程（`std.os.proc`）、套接字
（`std.net.tcp`）和流抽象（`std.os.io`），但「这是不是终端、窗口多大、怎么进
原始模式」、「怎么接信号」、「怎么用字节流把两个执行流接起来」、「怎么同时等
一堆 fd」这一层还空着。四个模块都是 POSIX 的薄封装，全部零分配：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.os.tty` | `is_tty`、窗口大小、原始模式 | `size` 失败不动输出参数（调用方的默认值不被踩）；`TtyMode` 保存整个 `termios`，`raw` 对非终端返回 0 且不改状态；`restore` 幂等（重复调用返回 0、无副作用）；只做本地终端，伪终端分配留给调用方直接调 libc |
| `std.os.signal` | `install`/`ignore`/`reset`、屏蔽、发送、名字↔编号 | 名字统一成动词，避开 libc 的 `signal`/`raise`/`kill`（同 TU 同名声明会打架）；`install` 带 `SA_RESTART`；`block`/`unblock` 改的是当前线程的屏蔽字，`is_pending` 查挂起集合；20 个常用信号的名字表，带不带 `SIG` 前缀都认 |
| `std.os.pipe` | 匿名管道 | 结构里带 `opened` 标记——`{ 0 }` 之后两个 fd 都是 0（stdin），不能靠 fd 判断是否打开；读/写都自己重试 `EINTR`，非阻塞用 `would_block()` 而不是让调用方比较 `errno`；`write_bytes` 返回实际字节数（可能部分写）；fd 带 `FD_CLOEXEC` |
| `std.os.poll` | 多路等待 | `PollSet` 是固定 32 槽的 `struct pollfd` 数组（不分配、不跨界）；`add` 同一个 fd 第二次是改事件，不会重复占槽；`wait` 返回就绪个数，`EVENT_HUP`/`EVENT_ERROR` 也算就绪；单 fd 的 `wait_readable`/`wait_writable` 是 1/0/-1 的便捷入口 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.os.tty`=0x6059、
`std.os.signal`=0x605a、`std.os.pipe`=0x605b、`std.os.poll`=0x605c；`grammar.md`
§8.1 的表同步。`Makefile` 增加 `std-term` 与 `STDTERM_SRCS`（四个模块都只依赖
`std/core/types.c6`，所以 `stdtest` 不需要额外依赖），`test-all`/`check-all` 都
带上它。示例 `examples/stdterm.c6` 用 `posix_openpt` 开一对伪终端做 tty 那节
（环境没有 `/dev/pts` 时只跳过这一节），其余三节只依赖管道，所以整份示例在任何
POSIX 环境里都能跑。

验收：`make stdtest`（93 个模块、0 failure）、`make std-term`（45 条自检全过）、
`make check-all`。

### 11.33 标准库第二十一批（udp / uds / resolver / httpc）

第二十一批补的是**网络客户端**：第十七批把报文层铺好了
（`std.net.dns` 的报文、`std.net.ws` 的帧、`std.net.tcp` 的连接），但「怎么发
一个数据报」、「不用 TCP 的两个本地进程怎么说话」、「域名怎么真的变成地址」、
「HTTP 响应怎么收完并解出 body」这一层还空着。四个模块仍然全部零分配：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.net.udp` | `udp_bind`/`udp_connect`、收发、超时、广播 | `recv_*` 的返回值分三种：字节数、`0`（空报文，不是错误）、`-1`（超时/错误）；缓冲区小就按 `cap` 截断并返回实际写入长度（`recv_from` 保证不越界）；`udp_connect` 只是记下默认对端（之后也能 `send_to` 别人）；`send_to` 用 `getaddrinfo`，主机名照样能用 |
| `std.net.uds` | `socketpair`、路径监听/连接、半关闭 | `Uds` 和 `Tcp` 分开是因为生命周期不同：路径 socket 要 `unlink`，抽象命名空间不落盘；`read_*` 到对端关闭返回 `0`（和 `std.os.io` 的「EOF 是 0」一致）；连接超时沿用 `std.net.tcp` 的非阻塞 connect + `poll` |
| `std.net.resolver` | DNS 查询客户端 | 只做「一次一问」：没有重试、没有 EDNS0、没有 DoT；`parse_reply` 把 id 不匹配、TC 置位（截断）、rcode 非 0、RR 不完整都当失败（返回 0/-1），`a`/`a16` 传 `0` 时只校验不提取；`resolver_conf_parse` 单独暴露，配置文本不用落盘也能测 |
| `std.net.httpc` | 请求构建、响应解析、chunked、发一次请求 | `build_request` 先量再写（放不下返回 -1 且 `dst` 一个字节不动）；`parse_response` 用 1/0/-1 三态把「完整/还不够/坏报文」分开，`fetch` 靠它决定继续读还是收工；chunked 解到调用方缓冲（零分配），支持扩展与 trailer；只做 `http://`，不跟重定向、不做 100-continue、HEAD 要自己用 `build_request` |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.net.udp`=0x605d、
`std.net.uds`=0x605e、`std.net.resolver`=0x605f、`std.net.httpc`=0x6060；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-client` 与 `STDCLIENT_SRCS`，
`test-all`/`check-all` 都带上它；`stdtest` 的依赖表补上 resolver（dns/udp/io/str）
与 httpc（tcp/url/str）。示例 `examples/stdclient.c6` 里两个「服务器」都用
`fork` 起在 127.0.0.1 上（一个回 DNS 响应、一个回 chunked 响应），所以整份示例
离线可跑。顺带修了一个宏污染：glibc 的 `<netdb.h>` 把 `NO_DATA` 定义成宏，
`import` 在导入方的宏环境里展开，于是 `std/core/err.c6` 的 `enum Error` 成员名
被替换掉；在 err 模块顶上 `#undef NO_DATA` 之后，同时 include `<netdb.h>` 并
import `std.os.io` 的 TU 也能编过。

验收：`make stdtest`（97 个模块、0 failure）、`make std-client`（36 条自检全过）、
`make check-all`。

### 11.34 标准库第二十二批（httpd / router / assets / session）

第二十二批把上一批的客户端接成了**服务端**：`std.net.http` 会解析报文、
`std.net.tcp` 会搬字节，但「怎么循环接受连接」「怎么按路径分发」「怎么安全地
把文件发出去」「怎么在无状态协议上记一次登录」这一层还空着。四个模块仍然零
分配：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.net.httpd` | 接受连接、收一个请求、写响应、关连接 | 一次连接一个请求（响应一律 `Connection: close`）——keep-alive 要维护连接状态机，不在这一层做；`read_request` 用 1/0/-1 区分「完整/对端直接关/坏请求」，body 只按 `Content-Length` 收，chunked 请求与 absolute-form 直接 400；`begin` 只写头、`send_body` 写 body，给静态文件流式发送留出口；处理器回调返回 0 时由 `serve_once` 回 404 |
| `std.net.router` | 方法与路径模式匹配 | 表是固定 32 条的数组、模式拷贝进结构体，参数值是从路径上切的视图（`Match` 不能活得比路径缓冲久）；`/` 严格切段，空段也参与比较，所以 `/a/`、`/a//b` 不会被悄悄归一；`*` 只能最后一段，`..` 不特殊——是否放行由 assets 决定 |
| `std.net.assets` | root 下的静态文件 | 安全边界都放在 `resolve`：先百分号解码（`%2e%2e` 也认），出现 NUL 或任何一段是 `..` 直接拒；`path_normalize` 按设计**保留** `..`，所以这一层选择「先拒再拼」，而不是拼完再猜；归一结果必须等于 root 或以 `root/` 开头；默认 `lstat` 拒符号链接，`set_follow_links(1)` 才跟随（跟随之后不保证链接指向哪儿）；8 KiB 流式写、不占堆 |
| `std.net.session` | 内存会话 + cookie | 16 个槽、id 是 16 字节随机数的 32 位十六进制（`std.core.rand`，明确**不是**密码学安全的）；数据是 256 字节里的 `key=value\n` 行，键不能含 `=`/空白/换行；`find` 顺手回收过期槽，`expire(now)` 把时钟当参数传进来所以测试不用 `sleep`；cookie 名固定 `sid`，`from_cookie` 认分号分隔的 Cookie 头、忽略属性 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.net.httpd`=0x6061、
`std.net.router`=0x6062、`std.net.assets`=0x6063、`std.net.session`=0x6064；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-serve` 与 `STDSERVE_SRCS`，
`test-all`/`check-all` 都带上它；`stdtest` 的依赖表补上 httpd（http/tcp/str）、
assets（httpd/http/tcp/mime/url/fs/io/path/str/err）、session（rand/time/cookie/
str），router 只借 `Str` 类型所以不用额外正文。示例 `examples/stdserve.c6` 用
`fork` 起一个本地服务端子进程连跑 7 个请求，客户端用第二十一批的
`std.net.httpc` 打回去，客户端与服务端在同一份示例里互相验证。

验收：`make stdtest`（101 个模块、0 failure）、`make std-serve`（28 条自检全过）、
`make check-all`。

### 11.35 标准库第二十三批（md5 / sha512 / hkdf / pbkdf2）

第二十三批是**纯计算**的一批：前面已经有 SHA-1/SHA-256，但「老数据的校验和
（MD5）」「更强的摘要（SHA-512/384）」「把共享秘密摊成密钥（HKDF）」「把口令
摊成密钥（PBKDF2）」还缺。四个模块都不碰 I/O、不依赖平台，全部用 RFC 向量
钉住：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.md5` | MD5 与流式句柄 | 只做校验和/兼容格式，文档头明确写「碰撞是分钟级的」；T 表按 `sin(i+1)` 生成后核对；长度按小端 64 位，`md5_eq` 是 16 字节常数时间比较 |
| `std.encoding.sha512` | SHA-512 / SHA-384 / HMAC-SHA-512 | 长度用 `n_hi`/`n_lo` 两个 u64 记字节数，收尾换成 128 位比特（跨 2^61 字节也不绕回）；SHA-384 不是「截 64 字节」，要 `init384` 换 IV；HMAC 的块长是 128（不是 SHA-256 的 64） |
| `std.encoding.hkdf` | RFC 5869 | 空 salt 按 RFC 用 32 个零字节（不是「不加盐」）；expand 的计数器只有一字节，所以上限是 255*32=8160，超了返回 -1；info/salt 长度任意，靠流式 HMAC 分片喂，不拼临时缓冲 |
| `std.encoding.pbkdf2` | PBKDF2-HMAC-SHA-1/SHA-256 | 迭代次数是安全参数，`iters <= 0` 直接拒绝（不默默降级成 1）；`U(1)=HMAC(P, S 拼上 INT(i))` 的 `INT(i)` 是大端 4 字节，测试用「iters=1 等于一次 HMAC」钉住；口令/盐传 0 当空串 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.md5`=0x6065、
`std.encoding.sha512`=0x6066、`std.encoding.hkdf`=0x6067、`std.encoding.pbkdf2`=0x6068；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-crypto` 与 `STDCRYPTO_SRCS`，
`test-all`/`check-all` 都带上它；`stdtest` 的依赖表补上 md5/sha512（bits）、
hkdf（sha256+bits+err）、pbkdf2（sha1+sha256+bits+err）。示例
`examples/stdcrypto.c6` 把四节串起来，最后用 HMAC-SHA-256 做一次 token 签发/
篡改检测——期望值全来自 RFC，所以 `make std-crypto` 的输出与平台无关。

验收：`make stdtest`（105 个模块、0 failure）、`make std-crypto`（28 条自检全过）、
`make check-all`。

### 11.36 标准库第二十四批（walk / temp / lock / mmap）

第二十四批补的是**文件系统进阶**：`std.os.fs` 已经给了 `stat`/`mkdir`/单层
`Dir`，但「遍历整棵树」「安全地拿临时名字」「多个进程协调同一个文件」「大文件
不整块读进堆」还缺。四个模块都是 POSIX 薄封装、零分配：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.os.walk` | 目录树遍历/统计/删除 | 在 `Dir` 上做深度优先递归，路径拼在 512 字节栈缓冲、递归上限 32 层；`max_depth` 定义成「最多访问到第几层」（1 = 只访问 root 的直属条目），回调返回 0 只对目录有意义（跳过子树）；`remove_tree` 后序删，符号链接只删链接本身 |
| `std.os.temp` | 临时文件与目录 | 只包 `mkstemp`/`mkdtemp`，不自己拼随机名（那是竞态和可预测名字的来源）；目录空时按 `$TMPDIR` → `/tmp`；文件 0600、`FD_CLOEXEC`，**不自动删除**（要删显式 `remove`），前缀里的 `/` 直接拒 |
| `std.os.lock` | 建议锁 | 只做整文件 `fcntl` 记录锁，不做区间锁；`acquire` 用 1/0/-1 区分「拿到/超时/出错」，`wait_ms < 0` 走 `F_SETLKW`、`>0` 按 5ms 重试；文档把「fcntl 锁按进程合并、子进程不继承、close 会释放该文件所有锁」三个坑写在最前面 |
| `std.os.mmap` | 文件/匿名映射 | `map_file` 映射整个文件（可写 `MAP_SHARED`、只读 `MAP_PRIVATE`），空文件直接给坏映射（`mmap` 不接受长度 0）；`read_at`/`write_at` 做边界检查，`data()` 不检查（和 C 指针一致）；匿名映射 `sync` 返回 0（没有盘可刷） |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.os.walk`=0x6069、
`std.os.temp`=0x606a、`std.os.lock`=0x606b、`std.os.mmap`=0x606c；`grammar.md`
§8.1 的表同步。`Makefile` 增加 `std-fsx` 与 `STDFSX_SRCS`，`test-all`/`check-all`
都带上它；`stdtest` 的依赖表补上 walk（fs/path/str/err）与 lock（time），
temp/mmap 自成一体（temp 只借 `Str` 类型）。示例 `examples/stdfsx.c6` 把四节
串成「临时文件 → 就地改字节 → 扫目录 → 跨进程抢锁」，全程只碰系统临时目录和
`build/`。

验收：`make stdtest`（109 个模块、0 failure）、`make std-fsx`（33 条自检全过）、
`make check-all`。

### 11.37 标准库第二十五批（aes / chacha20 / poly1305 / random）

第二十五批往密码学方向再走一步：上一批有哈希与 KDF（SHA-2/MD5/HKDF/PBKDF2），
但「真的加密一段数据」还缺分组密码、流密码、MAC 和安全随机。四个模块都是纯
计算（random 只读内核设备），零分配，测试向量全部来自 FIPS/NIST/RFC：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.aes` | AES-128/192/256 与 CTR | 只给分组原语 + 未认证的 CTR；S 盒/逆 S 盒/Rcon 由 GF(2^8) 的逆 + 仿射变换生成后写进表（生成脚本不进仓库）；CTR 的 counter 按整 16 字节大端递增（SP 800-38A 约定），最后一块可以不满；结构体只存 60 个轮密钥字，按值传递 |
| `std.encoding.chacha20` | RFC 8439 流密码 | 12 字节 nonce（IETF 变体）而不是原始的 8 字节；32 位块计数器，单 (key, nonce) 上限 256 GiB；`chacha20_xor` 允许原地加解密，计数器在函数内部按块推进，所以「一次 200 字节」和「64+64+72 手工推进」结果一致 |
| `std.encoding.poly1305` | RFC 8439 一次性 MAC | 5×26 位肢体做 130 位模运算：所有乘积用 u64 装、每轮把进位压回 26 位，**不需要 128 位整数**；key 的 clamp 折进 r 的掩码；文档最前面写清「同一把 key 只能用一次」 |
| `std.encoding.random` | `/dev/urandom` | 每次调用 open/close、不缓存 fd（不引入全局状态、fork 之后不共享）；`rand_below` 用「掩码 + 拒绝采样」而不是取模；返回 1/0 而不是假装成功 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.aes`=0x606d、
`std.encoding.chacha20`=0x606e、`std.encoding.poly1305`=0x606f、`std.encoding.random`=0x6070；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-cipher` 与 `STDCIPHER_SRCS`，
`test-all`/`check-all` 都带上它；`stdtest` 的依赖表补上 chacha20（bits），
aes/poly1305/random 自成一体。示例 `examples/stdcipher.c6` 前半段逐个验向量，
后半段用随机 key/nonce 做 ChaCha20 + Poly1305 的 encrypt-then-MAC 与篡改检测。

验收：`make stdtest`（113 个模块、0 failure）、`make std-cipher`（27 条自检全过）、
`make check-all`。

### 11.38 标准库第二十六批（bencode / msgpack / cbor / pem）

第二十六批补的是**数据格式**：`std.data` 已经有 JSON/CSV/INI/TOML/XML，但
「BitTorrent 的元信息」「两种紧凑二进制」「证书/密钥的 PEM 封装」还缺。四个
模块都是纯函数、零分配，写端写调用方缓冲、读端给输入上的视图：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.bencode` | 整数/字节串/列表/字典 | 读端给**标记流**（LIST/DICT/END），由调用方按自己的结构递归消费，不在库里建树；整数和长度都拒绝前导零，溢出返回 -1；字典键的字节序排序交给调用方（库不替它排） |
| `std.data.msgpack` | MessagePack | 写端对非负整数走无符号编码（和主流实现一致）；读端把 fixint/定型整数、fixstr/str8/16/32、bin8/16/32、float32/float64 都覆盖，扩展类型（`c7..c9`/`d4..d8`）与保留码 `c1` 一律 -1；float32 转 f64，Inf/NaN 当 0 |
| `std.data.cbor` | CBOR（RFC 8949 的确定长度子集） | 头是「主类型 + 附加信息」，`<24` 内联、24/25/26/27 跟 1/2/4/8 字节；读端支持主类型 0..5 与 `f4..f6`/`f9`/`fa`/`fb`，不定长（ai=31）、标签（主类型 6）、`f7`/`f8` 一律 -1——只做够覆盖配置/遥测的确定长度子集 |
| `std.data.pem` | PEM 封装 | 编码按 48 字节一块（正好 64 个 Base64 字符）逐行写，先算总长再落笔（放不下不写半截）；解码器容忍 CRLF、行首空白与前后噪声，标签必须首尾一致；不解析 DER 内部，只负责封装/拆封 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.msgpack`=0x6071、
`std.data.cbor`=0x6072、`std.data.bencode`=0x6073、`std.data.pem`=0x6074；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-data` 与 `STDDATA_SRCS`，
`test-all`/`check-all` 都带上它；`stdtest` 的依赖表补上 bencode/msgpack/cbor
（只借 `Str` 类型）与 pem（base64 + str）。示例 `examples/stddata.c6` 用同一份
带嵌套的小数据走四种格式，期望字节全部写在代码里。

验收：`make stdtest`（117 个模块、0 failure）、`make std-data`（15 条自检全过）、
`make check-all`。

### 11.39 标准库第二十七批（geometry / interp / stats / matrix）

第二十七批补的是**数值工具**：`std.core.math` 只有标量函数，`std.core.complex`/
`bignum`/`decimal`/`rational` 是数本身，但「向量怎么算、区间怎么映射、一组
样本的方差是多少、矩阵怎么解」这一层还空着。四个模块都是纯函数、零分配：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.geometry` | Vec2/Vec3/Rect | 值类型 + 自由函数（`vec2_add(a, b)` 读起来就是数学式，也符合「首参 `T *` 才是方法」的约定）；零向量单位化给 (0,0) 不产生 NaN；开方用牛顿迭代（32 次），不链 libm；`Rect` 负宽高翻正，`rect_intersect` 无交集给 `w == 0` |
| `std.core.interp` | 插值/重映射/缓动 | `lerp` 不钳制（外插合法），`lerp_clamped` 才钳；11 条缓动曲线的端点严格 f(0)=0、f(1)=1（测试逐条钉），`out_back` 中段按定义超过 1；sin 用 15 阶泰勒级数（端点误差 <1e-12），不链 libm |
| `std.core.stats` | 描述统计 | 流式用 Welford（比 `sum(x²)-n·mean²` 稳），一遍数据给均值/样本方差/总体方差/标准差/最值；分位数/中位数就地插入排序 + 线性插值（和 numpy 默认口径一致）；直方图左闭右开、右端点进最后一格；空样本全给 0 不产生 NaN |
| `std.core.matrix` | 行主序矩阵 | 缓冲由调用方给，`mat_strided` 允许行间有空洞（子矩阵/图像行走廊）；`det`/`solve` 用部分主元高斯消元，scratch 也由调用方给（零分配）；维度不匹配返回 0、参数错返回 -1；结果不能和入参重叠（不做临时拷贝） |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.stats`=0x6075、
`std.core.matrix`=0x6076、`std.core.geometry`=0x6077、`std.core.interp`=0x6078；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-calc` 与 `STDCALC_SRCS`，
`test-all`/`check-all` 都带上它；`stdtest` 的依赖表里四个模块都自成一体。
示例 `examples/stdcalc.c6` 把四节串成「采样 → 统计 → 线性拟合 → 坐标变换 →
缓动」的链路，全是纯计算。

验收：`make stdtest`（121 个模块、0 failure）、`make std-calc`（32 条自检全过）、
`make check-all`。

### 11.40 标准库第二十八批（deflate / zlib / gzip / tar）

第二十八批补的是**压缩与归档**：`std.encoding` 已经有 LZ4/RLE/Base64/SHA/AES，
但「zlib/gzip 那种 DEFLATE 流」和「tar 归档」还缺。四个模块零分配、写调用方
缓冲，解压侧覆盖真实世界的 gzip（动态 Huffman），压缩侧给出可用的 fixed
Huffman：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.deflate` | DEFLATE（RFC 1951） | `inflate` 三种块（stored/固定/动态）全支持：回溯直接读调用方输出缓冲（不额外开 32 KiB 窗口），动态表用 puff 的 count/symbol 规范解码；压缩侧分两档：`deflate_stored`（不压缩但一定合法）与 `deflate_fixed`（贪心 LZ77 + 固定 Huffman，4096 槽单候选哈希表在栈上，够压重复文本，压缩率不如 zlib 的链表+动态树） |
| `std.encoding.zlib` | RFC 1950 容器 | 头固定 `78 01`；解压校验 CM=8、CINFO≤7、`(CMF<<8|FLG)%31==0`、拒绝 FDICT；Adler-32 模 65521 覆盖解压后的数据 |
| `std.encoding.gzip` | RFC 1952 容器 | 只写 10 字节最小头（不写文件名/注释）；解压按 FLG 跳过 FEXTRA/FNAME/FCOMMENT/FHCRC；页脚校验 CRC-32 与 ISIZE（原长度 mod 2^32）；CRC 走 `std.core.crc` 的 `crc32_update` |
| `std.encoding.tar` | ustar 归档 | 读端校验 checksum（chksum 字段按 8 个空格算）、拼 prefix/name、数据给视图、坏八进制/截断返回 -1；写端 512 对齐、八进制字段、结尾两个零块；名字超过 100 字节就拆进 prefix（各 ≤100/155） |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.deflate`=0x6079、
`std.encoding.zlib`=0x607a、`std.encoding.gzip`=0x607b、`std.encoding.tar`=0x607c；
`grammar.md` §8.1 的表同步。`Makefile` 增加 `std-arch` 与 `STDARCH_SRCS`，
`test-all`/`check-all` 都带上它；`stdtest` 的依赖表补上 zlib（deflate）、
gzip（deflate+crc）、tar（str），deflate 自成一体。示例 `examples/stdarch.c6`
按依赖顺序串起四节，最后做一次 `.tar.gz` 的完整链路。

验收：`make stdtest`（125 个模块、0 failure）、`make std-arch`（22 条自检全过）、
`make check-all`。

### 11.41 标准库第二十九批（wav / png / elf / zip）

第二十九批补的是**读别人写的二进制**：`std.data` 已经有 JSON/CSV/TOML/XML/
Bencode/MessagePack/CBOR/PEM，但媒体容器（WAV）、图片（PNG）、可执行文件
（ELF）、归档（ZIP）还没有。四个模块都是「零分配、只读不拷贝」，只解析结构，
不做像素/样本/重定位解码：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.wav` | RIFF/WAVE 头 | `fmt ` 解析格式/声道/采样率/位宽/对齐，`data` 记录偏移与长度；未知块按 `size` + 奇数补齐跳过；`fmt` 必须先于 `data`（否则 `valid=0`），不做 WAVE64/RF64 |
| `std.data.png` | PNG 块 | 8 字节签名 + 每块 `长度/类型/数据/CRC`；CRC 用 `std.core.crc` 的 IEEE 多项式；CRC 错只置 `crc_ok=0` 不中止；`png_parse` 只认第一块 IHDR（13 字节）；不做 zlib 解压/滤波 |
| `std.data.elf` | ELF 头 + 节表 | 支持 ELF32/64、小端/大端；`shnum`/`shentsize` 决定节表范围，越界整份判无效；节名从 `shstrndx` 字符串表取（超 63 字节截断），不做程序头/符号表/重定位 |
| `std.data.zip` | ZIP | 从尾部扫 EOCD（注释 ≤65535），按中央目录逐项读元数据，再按本地头偏移取数据；stored 拷贝、deflate 走 `std.encoding.deflate`，校验 CRC-32；ZIP64/加密/未知方法返回 -1 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.wav`=0x607d、
`std.data.png`=0x607e、`std.data.elf`=0x607f、`std.data.zip`=0x6080；`grammar.md`
§8.1 的表同步。`Makefile` 增加 `std-bin` 与 `STD_BIN_SRCS`，`test-all`/`check-all`
都带上它；`stdtest` 的依赖表补上 png（crc+str）、zip（deflate+crc），wav/elf
自成一体。示例 `examples/stdbin.c6` 嵌了四段真实字节（Python 的
`wave`/`zlib`/`struct`/`zipfile` 生成），逐段自检并把 PNG 的 IDAT 交给
`std.encoding.zlib` 解出像素。

验收：`make stdtest`（129 个模块、0 failure）、`make std-bin`（20 条自检全过）、
`make check-all`。

### 11.42 标准库第三十批（bmp / tga / qoi / pnm）

第三十批继续补 `std.data` 的媒体解析：上一批做了 WAV/PNG/ELF/ZIP，这一批把
另外四种「结构简单、没有大表」的图像格式补齐，全部解成 RGBA8888：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.bmp` | Windows BMP | BITMAPINFOHEADER（也接受 V4/V5 头的前 40 字节）、24/32 位 BI_RGB、8 位调色板（`clrUsed`）、自下而上/自上而下、行 4 字节对齐；BI_RLE/BI_BITFIELDS/16 位/OS2 核心头不支持 |
| `std.data.tga` | Truevision TGA | 类型 2（未压缩）与 10（RLE）真彩、24/32 位、四种原点；RLE 包按一维像素流处理（允许跨行），包越界/截断 -1；调色板与灰度类型不支持 |
| `std.data.qoi` | Quite OK Image | 解码支持 INDEX/DIFF/LUMA/RUN/RGB/RGBA 与结束标记；编码器只发 RUN/INDEX/RGB/RGBA（不生成 DIFF/LUMA，合法但略大）；两侧都是 RGBA8888，零分配 |
| `std.data.pnm` | Netpbm P5/P6 | 注释、maxval 1..65535、8/16 位（大端）样本，按 `v*255/maxval` 缩放；P1..P4/P7 与多图不支持 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.bmp`=0x6081、
`std.data.tga`=0x6082、`std.data.qoi`=0x6083、`std.data.pnm`=0x6084；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-img` 与 `STD_IMG_SRCS`，`test-all`/`check-all` 都
带上它；四个模块在 `stdtest` 里都自成一体（`deps=""`）。示例
`examples/stdimg.c6` 让四个解码器读同一张 2x2 图并逐字节比对 RGBA，QOI 那一节
再做一次编码→解码往返。

验收：`make stdtest`（133 个模块、0 failure）、`make std-img`（16 条自检全过）、
`make check-all`。

### 11.43 标准库第三十一批（asn1 / x509 / jwt / otp）

第三十一批补「凭据」这条线：DER 读取器打底，上面架证书与 JWT，另加一次性口令。
四个模块都零分配，只依赖已有的哈希/Base64：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.asn1` | DER 读取器 | `asn1`/`asn1_sub` + `next`/`peek` 切 `Asn1Tag`（class/tag/内容偏移/长度），支持多字节 tag 号与 0x81..0x84 长长度，拒绝不定长与越界；值解码只给 INTEGER（≤8 字节）/OID/BIT STRING/UTCTime·GeneralizedTime/三种字符串；不校验 DER 规范性 |
| `std.data.x509` | X.509 v3 证书解析 | 拆 `tbsCertificate`/`signatureAlgorithm`/`signatureValue`，取版本、序列号（十六进制）、签名 OID、issuer/subject 的 CN·O·C、有效期、SPKI 视图、RSA 模数位数、签名视图；不验签/不验链/不处理 PEM，坏结构 `valid=0` |
| `std.data.jwt` | JWS 紧凑格式验签 | 只支持 HS256/HS384/HS512：Base64URL 解头与载荷、按头里的 `alg` 选 HMAC、对 `h.p` 常量时间比较签名；`alg=none` 与非 HMAC 算法一律拒绝；不解析 JSON、不校验 `exp`/`nbf` |
| `std.encoding.otp` | HOTP / TOTP | HMAC-SHA-1 + 动态截断，`hotp`/`totp`/`otp_format`/`otp_verify_totp`（前后各 `window` 步）；位数 4..9，密钥是原始字节（Base32 走 `std.encoding.base32`）；不做防重放 |

这一批另外给 `std.encoding.sha512` 补了 `hmac_sha384`（原先只有 `sha384` 一次性
哈希与 `hmac_sha512`，JWT 的 HS384 需要真正的 SHA-384 IV + 48 字节中间值）。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.asn1`=0x6085、
`std.data.x509`=0x6086、`std.data.jwt`=0x6087、`std.encoding.otp`=0x6088；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-enc` 与 `STD_ENC_SRCS`，`test-all`/`check-all` 都带上
它；`stdtest` 的依赖：asn1 借 `Str` 类型，x509 要 asn1，jwt 要 base64 + sha256 +
sha512 + bits，otp 要 sha1 + bits。示例 `examples/stdenc.c6` 先走手写 DER，再解
829 字节自签证书、验 HS256 令牌、打 RFC 4226/6238 的 OTP 码。

验收：`make stdtest`（137 个模块、0 failure）、`make std-enc`（24 条自检全过）、
`make check-all`。

### 11.44 标准库第三十二批（lzw / gif / tiff / ico）

第三十二批补图像这条线里剩下的常见格式，外加它们共用的 LZW：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.lzw` | GIF 变体 LZW | LSB 打包、clear/eoi、码宽 2..12 位；解码端比编码端晚一个条目建表，所以码宽早一格长（`next == (1<<size)-1`）；编码器贪心最长匹配 + 线性扫字典（数据量小，代码小）；不处理 TIFF/Unix compress 的 MSB 变体 |
| `std.data.gif` | GIF87a/89a | 全局/局部调色板、图形控制扩展（透明色/延迟/处置）、隔行扫描 4 遍重排、多帧按索引取；LZW 直接解进调用方的 RGBA 缓冲再倒着展开（省一次拷贝）；不合成画布 |
| `std.data.tiff` | 基线 TIFF | II/MM 字节序、STRIP、8 位 chunky、none/Deflate(zlib)/PackBits、RGB 与两种灰度、1/3/4 样本；LZW/JPEG/16 位/分平面/分块一律 -1；单条带临时区上限 64KB |
| `std.data.ico` | Windows .ico | 目录项 + 32/24/8/4/1 位 DIB（自下而上、BGRA、AND 掩码透明、32 位 alpha 全 0 按不透明）或内嵌 PNG（分派给 `png_decode`）；.cur 与 16 位 DIB 不支持 |

顺带给 `std.data.png` 补了真正的像素解码 `png_decode`：8 位、非隔行、颜色类型
0/2/3/4/6、五种滤波（None/Sub/Up/Average/Paeth）、tRNS 透明色与调色板 alpha，
输出 RGBA8888；16 位、1/2/4 位、Adam7 隔行返回 -1。ICO 的 PNG 负载与示例都靠它。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.lzw`=0x6089、
`std.data.gif`=0x608a、`std.data.tiff`=0x608b、`std.data.ico`=0x608c；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-gfx` 与 `STD_GFX_SRCS`，`test-all`/`check-all` 都带上
它；`stdtest` 的依赖：gif 要 lzw，tiff 要 zlib + rle，ico 要 png，lzw 自成一体。
示例 `examples/stdgfx.c6` 做 LZW 往返、解 GIF/TIFF、用同一张图的两个 ICO 负载
逐字节比对，再把 ICO 里的 PNG 单独解一遍。

验收：`make stdtest`（141 个模块、0 failure）、`make std-gfx`（18 条自检全过）、
`make check-all`。

### 11.45 标准库第三十三批（html / yaml / vcf / ics）

第三十三批把「结构化文本」这条线补齐到 Web 与个人信息格式：

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.html` | HTML 分词器 | 六种 token（文本/开始/结束/注释/DOCTYPE/CDATA）；属性区只记原文偏移，`html_attr`/`html_attr_at` 按需扫，token 是可拷的小结构；`script`/`style`/`textarea`/`title` 走原文模式；`html_unescape` 只认命名实体与数字实体；不建 DOM、不给选择器 |
| `std.data.yaml` | YAML 子集 | 扁平事件 + `depth`（缩进/2），调用方自己还原层级；注释、单/双引号、文档分隔符、`- 键: 值`；不支持流式 `{}`/`[]`、锚点、标签、块标量；制表符缩进直接 -1 |
| `std.data.vcf` | vCard | 两段式：`vcard_unfold` 摊平折行（要写缓冲），`vcard_parse` 在摊平文本上取 FN/N/ORG/TITLE/TEL/EMAIL/UID/URL；参数忽略，必须成对 BEGIN/END |
| `std.data.ics` | iCalendar | 同样两段式；`next` 每个 VEVENT 返回一个事件，`TZID`/`VALUE=DATE` 参数忽略、值原样给出；不做时区换算 |

两个「折行」格式（vCard/iCalendar）都用「CRLF + 空白」续行，摊平必须去掉那
一个空白但保留行分隔，所以单独给 `*_unfold`，而不是在解析器里就地处理——
就地处理需要拼接，零分配做不到。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.html`=0x608d、
`std.data.yaml`=0x608e、`std.data.vcf`=0x608f、`std.data.ics`=0x6090；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-doc` 与 `STD_DOC_SRCS`，`test-all`/`check-all` 都
带上它；四个模块在 `stdtest` 里只依赖 `std/text/str.c6`。示例 `examples/stddoc.c6`
把四个读取器串起来跑（23 条自检）。

验收：`make stdtest`（145 个模块、0 failure）、`make std-doc`（23 条自检全过）、
`make check-all`。

### 11.46 标准库第三十四批（sse / socks5 / mqtt / snappy）

第三十四批全是「字节层编解码」：只填缓冲、只解析缓冲，套接字留给
`std.net.tcp` 或调用方。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.net.sse` | Server-Sent Events 流 | 空行派发、四个字段、多行 `data` 拼进调用方缓冲（零分配做不到拼接）；注释忽略、BOM 跳过、三种行结束符都认；EOF 前没空行收尾的半截事件按规范丢弃；单行上限 8 KB |
| `std.net.socks5` | SOCKS5 报文 | 问候/用户名口令/请求三种构建 + 方法/认证/应答/地址四种解析；`Socks5Addr` 支持 IPv4/IPv6/域名，域名用 `inet_parse` 试探（失败才当域名）；半包返回 0、坏包 -1 |
| `std.net.mqtt` | MQTT 3.1.1 报文 | 固定头 + 剩余长度变长整数；CONNECT（含遗嘱与账号）/PUBLISH（QoS 0/1/2）/SUBSCRIBE/各类确认的构建与解析，`mqtt_topic_matches` 做 `+`/`#` 通配；不做 TLS、重传与遗嘱触发 |
| `std.encoding.snappy` | Snappy 块格式 | 解码认四种元素并严格校验（重叠复制逐字节搬）；编码用调用方给的 16 KB 哈希表做贪心匹配，单次匹配封顶 64 字节；不给哈希表就退化成整块字面量（仍是合法流）；不做 framed 与 CRC-32C |

「半包返回 0、坏包返回 -1」是这四个模块统一的返回值约定，网络读到一半的常见
情况不用靠 `errno` 表达，调用方 `while ((n = next(...)) == 0) read_more();` 就行。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.net.sse`=0x6091、
`std.net.socks5`=0x6092、`std.net.mqtt`=0x6093、`std.encoding.snappy`=0x6094；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-proto` 与 `STD_PROTO_SRCS`，
`test-all`/`check-all` 都带上它；`stdtest` 里 sse/mqtt 只依赖 `std/text/str.c6`，
socks5 还要 `std/net/inet.c6`，snappy 自成一体。示例 `examples/stdproto.c6` 把四个
编解码器串起来跑（21 条自检）。

验收：`make stdtest`（149 个模块、0 failure）、`make std-proto`（21 条自检全过）、
`make check-all`。

### 11.47 标准库第三十五批（cpio / pe / macho / wasm）

第三十五批看二进制容器：归档格式与三种可执行/字节码格式，都只解析结构。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.cpio` | cpio 归档 | 只认 newc（`070701`）与 crc（`070702`）：110 字节 ASCII 十六进制头 + NUL 结尾的名字 + 4 字节对齐的数据；`TRAILER!!!` 返回 0；odc 与旧二进制格式不支持（现实里 rpms/initramfs 都是 newc） |
| `std.data.pe` | PE / COFF | DOS 头 → `e_lfanew` → `PE\0\0` → COFF → 可选头，PE32 与 PE32+ 的 `ImageBase` 宽度分开处理；节表按 40 字节遍历；`pe_rva_to_off` 只在节的**原始数据**范围内给偏移（`.bss` 那种只有虚拟大小的段落返回 0，这点最容易写错） |
| `std.data.macho` | Mach-O | thin 32/64 位小端 + fat 外壳；`LC_SEGMENT`/`LC_SEGMENT_64` 的固定部分 56/64 字节、节 68/80 字节，`nsects` 在命令内偏移 48/64；抓 `LC_UUID` 与 `LC_MAIN`；大端映像不支持 |
| `std.data.wasm` | WebAssembly | 只做「模块 → 节 → 计数/导出项」，u32 LEB128 上限 5 字节；`wasm_count` 只对以元素个数开头的节有效；不解析函数体 |

两个坑值得记一笔：PE 的 `e_lfanew` 是 u32，`(int)lf + 24` 会溢出成负数绕过越界
检查（现在用 u64 算），以及 Mach-O 的 `nsects` 在段命令里是 `off+48`（32 位）/
`off+64`（64 位），不是「命令末尾减去 flags」——按后者算会把 `flags` 当成节数。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.cpio`=0x6095、
`std.data.pe`=0x6096、`std.data.macho`=0x6097、`std.data.wasm`=0x6098；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-exec` 与 `STD_EXEC_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`。示例
`examples/stdexec.c6` 在内存里拼出四种容器再解析（15 条自检）。

验收：`make stdtest`（153 个模块、0 failure）、`make std-exec`（15 条自检全过）、
`make check-all`。

### 11.48 标准库第三十六批（bson / ubjson / plist / markdown）

第三十六批补序列化与标记：两种二进制 JSON（BSON / UBJSON）、Apple 的二进制
属性列表，以及一个 Markdown → HTML 渲染器。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.bson` | BSON | 文档 = int32 总长度 + 元素 + `0x00`；元素 = 类型 + cstring 名 + 值；`next` 只切视图，访问器才解码；int32 要**手动符号扩展**；子文档/数组共用 `bson_open` |
| `std.data.ubjson` | UBJSON | 事件流 + 容器栈：`$`（统一类型）与 `#`（计数）两个优化标记让后续元素**不带类型标记**，是最容易写错的地方；计数读满自动补 `UBJ_END`；深度上限 16 |
| `std.data.plist` | 二进制 plist | 32 字节尾部给 `offset_size`/`ref_size`/对象数/根/偏移表；对象之间用引用号相连，所以访问器都要 `(buf, n, info)`；`0x0F` 长度走「后面跟一个整数对象」；UTF-16 串不转码 |
| `std.text.markdown` | Markdown → HTML | 流式渲染，零分配；列表项**延迟**写 `</li>`，好让后续正文行并进同一项；行内渲染递归处理嵌套强调；未配对的 `*`/`**` 原样输出（两侧带空格不认作强调） |

三个坑：BSON 的字符串长度**不含**自己那 4 字节（子文档长度**含**），一开始把
两者当成一回事会让整个字符串被切掉；UBJSON 的 `$` 之后长度**仍带标记**（只有值
不带），测试向量写成 `Sia` 是对的、写成 `ia` 才对应 `$S`；Markdown 的
段落合并要在行内渲染里把换行变成空格，否则 `<p>` 里会出现真换行。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.bson`=0x6099、
`std.data.ubjson`=0x609a、`std.data.plist`=0x609b、`std.text.markdown`=0x609c；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-ser` 与 `STD_SER_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`。
示例 `examples/stdser.c6` 四种格式各一节（14 条自检）。

验收：`make stdtest`（157 个模块、0 failure）、`make std-ser`（14 条自检全过）、
`make check-all`。

### 11.49 标准库第三十七批（sysinfo / resource / user / pty）

第三十七批看系统本身：内核与负载、资源限制与用量、用户/组、伪终端。四个都是
libc 薄封装，重点在把 errno 变成明确的 1/0/-1 语义、把结构体拍平成 `long long`。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.os.sysinfo` | uname / uptime / loadavg / ncpu / pagesize | `Str` 视图指向模块内静态缓冲区（下次调用即失效），换来零分配；`getloadavg` 的三个参数都可以传 0 |
| `std.os.resource` | rlimit / rusage / nice | `RL_INFINITY` 是 -1（`rlim_t` 是无符号，直接比较会踩坑）；`getrusage` 的秒+微秒合成毫秒；`nice(2)` 成功也可能返回 -1，所以不拿返回值判成败 |
| `std.os.user` | passwd / group 查询 | 查不到返回 0、出错返回 -1（这个区别调用方常常要）；只给主组，不展开附加组 |
| `std.os.pty` | posix_openpt 一对 + fork 子进程 | `posix_openpt`/`grantpt`/`unlockpt`/`ptsname` 在严格 C99 下不声明，文件头要先 `#define _XOPEN_SOURCE 600`；子进程里 `setsid` + `TIOCSCTTY` 才能真正拿到控制终端；`pty_recv` 用 `poll` 给超时 |

两个坑：一是 pty 的**回显**——往 master 写的数据会被行规程回显回 master，测试里
从 master 读时必须先跳过回显、并且按「累积后再找子串」判，按「一次 read 的开头
匹配」会随机失败；二是 `nice(2)` 的返回值不能当错误码用。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.os.sysinfo`=0x609d、
`std.os.resource`=0x609e、`std.os.user`=0x609f、`std.os.pty`=0x60a0；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-sys` 与 `STD_SYS_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`。示例
`examples/stdsys.c6` 四节（13 条自检）。

验收：`make stdtest`（161 个模块、0 failure）、`make std-sys`（13 条自检全过）、
`make check-all`。

### 11.50 标准库第三十八批（sha3 / blake2s / crc32c / xxhash）

第三十八批补哈希与校验：Keccak 家族的 SHA-3/SHAKE、BLAKE2s、CRC-32C 与 xxHash。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.sha3` | SHA3-224/256/384/512 + SHAKE128/256 | Keccak-f1600 的 θ/ρ/π/χ/ι 五步按 `st[x+5y]` 展平；吸收按字节写进 64 位 lane（`(pos%8)*8` 移位），这样 rate 不必是 8 的倍数；SHAKE 的挤压位置单独记，`shake_out` 可连续调用 |
| `std.encoding.blake2s` | BLAKE2s（1~32 字节输出） | update **不压最后一块**（压了就丢 `last` 标记），留给 final；t 计数在压块前累加；正好 64 字节时 `pos==64` 由 final 处理 |
| `std.encoding.crc32c` | CRC-32C | 反射多项式 0x82F63B78；`crc32c` 自己管初值/末取反，`crc32c_update` 是裸寄存器接口，两种接口要能对上 |
| `std.encoding.xxhash` | XXH32/XXH64 | 一次性与流式两套；流式 `digest` 只读状态，可重复调用；`total >= stripe` 才走 accumulator 合并路径，短输入用 `seed + P5` |

两个坑：一是「先解引用再判空」——`xxh32_digest` 一开头就 `self->buf`，传 0 直接
段错误，把判空提到最前面；二是 SHAKE 的填充时机——必须在**第一次挤压**前补
（用 `squeezed` 标记），不能放在 `shake_out` 的循环里按「位置是否为 0」猜。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.sha3`=0x60a1、
`std.encoding.blake2s`=0x60a2、`std.encoding.crc32c`=0x60a3、`std.encoding.xxhash`=0x60a4；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-hash` 与 `STD_HASH_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`。
示例 `examples/stdhash.c6` 四节（17 条自检）。

验收：`make stdtest`（165 个模块、0 failure）、`make std-hash`（17 条自检全过）、
`make check-all`。








