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

### 11.22 `!error` 支持浮点返回类型

C9 原先在声明处拒绝 `float` / `double` / `long double`：失败路径要么用 `-1`
哨兵（浮点的 `-1` 位型是 NaN），要么按聚合清零，两者都不适用。现在把浮点并进
聚合那条路：`c6_emit_propagate` 与 `c6_async_propagate` 对 `is_float()` 的返回
类型改为清零局部槽再返回（全零位型 = `+0.0`），C99 后端与 native 后端共用同一
套 vstack 操作。整数类标量仍是 `-1` 哨兵；数组仍被 C9 拒绝（不是 C 的返回类型），
诊断用例改成数组。失败值本身仍不可观测（C15 强制消费），所以这只影响“编译器
能不能构造出失败值”，不改变错误槽语义。

验收：`tests/run-tests.sh try`（22 条）、`tests/run-tests.sh errors/try`
（17 个片段）、`make check-all`。
