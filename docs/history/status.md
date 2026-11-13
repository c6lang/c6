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
| 12 `async`/`await` 无栈协程 | ✅ | `parse_btype()` 的 `TOK_C6_ASYNC`（`FuncAttr.func_async`）→ `c6_async_declare`（用合成 token 在文件作用域声明 `struct NAME_task`）；`c6_async_definition` 把函数体按 `int __c6_body_NAME(void *task, void *frame)` 编译，`c6_async_func_begin` 生成恢复分发序言（非 0 状态就 `__c6_co_restore` + 跳恢复点）、`c6_async_params_copy` 把形参落成普通局部、`c6_async_func_end` 生成恢复分发器、`c6_async_finish` 写 `val` 并返回 `KIND_DONE`；`c6_async_call`（`unary()` 的 `tok_identifier` 分支）把调用改写成 `__c6_co_new` + 实参拷贝；`c6_await_parse`（`unary()` 前缀分支）+ `c6_async_suspend` 把错误槽 park 出去（帧住在任务里，不用搬，清理状态跟着帧走）；运行期 `libtcc1.c` 的 `__c6_co_new`（一次分配任务结构 + 帧）/ `__c6_co_await` / `__c6_co_suspend` / `__c6_co_restore` / `__c6_co_finish` / `__c6_co_wait` / `__c6_co_destroy`（按需 malloc + 手写 `epoll`/`poll`，不依赖 libc）；预置声明在 `tccpp.c` 的 `__TCC_ASYNC_DEFINED` 段 |

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
  点才回到下一条语句。`await c6_io` 总要停一轮（唤醒是下一轮 `epoll_wait()`
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
- `c6_io` 的 fd 为负数时任务按失败回收（`__c6_co_wait` 返回 `-1`，
  `__c6_co_run` 计一次失败），不会永远 park；非负 fd 没有上限（`epoll(7)`）；
- 调度是**协作式**的：没有抢占，不 hook libc，阻塞的 `read`/`printf` 不会
  自动变成让出点；`__c6_co_run()` 对顶层任务做轮转，一次 `epoll_wait()`
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

### 11.51 标准库第三十九批（pcx / hdr / xbm / xpm）

第三十九批收四种「老图形格式」：PCX、Radiance RGBE、XBM、XPM。它们都不大，
但各有各的历史包袱，正好把「读头 + 解 RLE + 位序/调色板」这类细节吃一遍。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.pcx` | PCX | 128 字节头；RLE 只看 `0xC0` 掩码；每行按 `bytes_per_line`（含填充）读、按 width 用；8 位单平面优先用文件末尾的 `0x0C`+768 字节 VGA 调色板，没有才退回头里 16 色 |
| `std.data.hdr` | Radiance RGBE | 头字段白名单（`FORMAT`/`EXPOSURE`/`GAMMA`…），分辨率行 `±Y h ±X w` 只取绝对值；扫描线「新式 RLE」靠 `2 2 hi lo` 自识别；老式平铺里的 `1 1 1 count` 重复标记要展开，所以**不能**预先按 `w*4` 卡行长度 |
| `std.data.xbm` | XBM | 尺寸来自 `#define *_width` / `*_height`；花括号里的数值十进制/十六进制都收；位序按 X11（每字节低位在左） |
| `std.data.xpm` | XPM | 第一个字符串是 `w h ncolors cpp`；颜色行按 cpp 字节键 + `c` + 颜色；像素行长度必须等于 `w*cpp`；16 位色取高字节 |

坑记两个：PCX 的尺寸是 `xmax-xmin+1`，所以「1 像素高」是 `ymax==ymin`——判非法
得用 `xmax < xmin` 而不是 `<=`；HDR 的老式行可能比 `w*4` 短，提前按长度卡会把
带重复标记的合法文件判坏。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.pcx`=0x60a5、
`std.data.hdr`=0x60a6、`std.data.xbm`=0x60a7、`std.data.xpm`=0x60a8；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-legacy` 与 `STD_LEGACY_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`。示例
`examples/stdlegacy.c6` 四节（24 条自检）。

验收：`make stdtest`（169 个模块、0 failure）、`make std-legacy`（24 条自检全过）、
`make check-all`。

### 11.52 标准库第四十批（bdf / woff / svg / ttf）

第四十批拐到「字体与矢量」：位图字体的老格式 BDF、Web 字体容器 WOFF、矢量路径
（SVG 的 `d`）与 TrueType 度量。四者共同点是**表/段目录先行**，正文按偏移取。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.bdf` | BDF | 关键字白名单式扫描；`bbw/bbh` 决定每行 `ceil(w/8)` 字节；位图按十六进制两字符一字节、高位在左；`ENCODING -1` 的复合字形返回 0 跳过 |
| `std.data.woff` | WOFF 1.0 | 只解容器：`compLength < origLength` 的表原样交出偏移与长度，解压交给 `std.encoding.zlib`；校验 `length` 不小于目录长度、每表偏移不越界 |
| `std.data.svg` | SVG path | 全部分量绝对化，`H/V`→`L`，`S`/`T` 反射上一控制点，`M` 后隐式 `L`；`A` 保留七参数不拆贝塞尔；包围盒用**控制点凸包**，弧按端点 ±`rx`/`ry` 外扩，因此只大不小 |
| `std.data.ttf` | TTF/OTF | 只读 `head`/`hhea`/`maxp`/`cmap`/`hmtx`；`cmap` 只挑 `(3,1)`、`(3,10)` 子表的 format 4/12；`ttf_glyph` 未映射返回 -1（0 是 `.notdef`） |

BDF 的十六进制行有个小坑：一行写奇数个 nibble 是坏数据（要报错），但不能因为
行比 `ceil(w/8)` 长就拒绝——有些生成器会补尾随空白。SVG 的 `A` 起点并不在参数
里，包围盒要用解析器当前的 `cx/cy`，所以 `svg_path_bbox` 必须和 `svg_next` 共用
同一套状态。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.bdf`=0x60a9、
`std.data.woff`=0x60aa、`std.data.svg`=0x60ab、`std.data.ttf`=0x60ac；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-font` 与 `STD_FONT_SRCS`（链接带 `-lm`，因为
`svg` 用 `pow`/`fabs`），`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里
都只依赖 `std/text/str.c6`。顺带修掉上一批文档表格里 `std/data/pnm.c6` 那一行被
接到 `xpm` 行末尾的问题。

验收：`make stdtest`（173 个模块、0 failure）、`make std-font`（24 条自检全过）、
`make check-all`。

### 11.53 标准库第四十一批（mbr / fat / iso / ext2）

第四十一批做「分区与文件系统」的头解析：MBR/GPT、FAT 引导扇区、ISO 9660 主卷、
ext2 超级块。四个都是纯读结构的活，正好把「偏移表 + 双端序 + 目录扫描」练一遍。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.mbr` | MBR + GPT | MBR 只认 `0x55AA` 签名与 4 个 16 字节目录；`0xEE` 类型视为 GPT 保护分区；GPT 头校验 `EFI PART`、`hsize` 92..512、`entry_size` 128..4096；表项名字是 UTF-16LE，非 ASCII 换成 `?` |
| `std.data.fat` | FAT12/16/32 | FAT 类型按**簇数阈值**判（4085 / 65525），不信 BPB 里的类型字符串；`data_start_sector = reserved + nfats*fat_size + 根目录扇区`；目录项跳过 `0xE5`、`attr==0x0F`、卷标 |
| `std.data.iso` | ISO 9660 | 主卷描述符固定在逻辑扇区 16；块大小在偏移 128 的双端序里，**大端那一半在 130**（读 128 会得到 8）；目录记录长度为 0 表示本块用尽，要跳到下一个块边界 |
| `std.data.ext2` | ext2/3/4 | 超级块在字节偏移 1024；块大小 = `1024 << log_block_size`；块组描述符表在 `first_data_block + 1` 个块处；inode 号**从 1 起**（2 号是根目录） |

两个坑：FAT 如果只清 `fat_size16` 而忘了 `total16`，簇数会按旧的 16 位总扇区算出
来，类型判断就错成 FAT12；ISO 的双端序字段是「小端在前、大端在后」，16 位字段的
大端副本在 +2 处，别直接读首地址。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.mbr`=0x60ad、
`std.data.fat`=0x60ae、`std.data.iso`=0x60af、`std.data.ext2`=0x60b0；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-disk` 与 `STD_DISK_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`。示例
`examples/stddisk.c6` 四节（28 条自检）。

验收：`make stdtest`（177 个模块、0 failure）、`make std-disk`（28 条自检全过）、
`make check-all`。

### 11.54 标准库第四十二批（base91 / uu / yenc / ebcdic）

第四十二批回到文本编码，但挑的是「老式传输编码」：basE91、uuencode、yEnc 与
EBCDIC。它们都带着上世纪的包袱，正好把「位宽不定 + 转义 + 折行」这几件事凑齐。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.base91` | basE91 | 每 13 位吐两个字符；低位 13 位的值 > 88 时只用 13 位，否则用 14 位。解码必须按「这一对的低 13 位是否 > 88」回头判断位宽，不能死按 14 位切 |
| `std.encoding.uu` | uuencode | 每行 45 字节、行首 `(n&0x3F)+0x20`、6 位值 0 写成 `` ` ``（避开行首空格）；解码跳过 `begin`/`end` 行，长度字符 `` ` ``/空格算 0 |
| `std.encoding.yenc` | yEnc | 字节 `+42`，若结果为 `0x00`/`0x0A`/`0x0D`/`0x3D` 则写 `=` + `(结果+64)`；解码时转义字符要**先减 64 再减 42**（等价于减 106），只减 64 会得到错误字节 |
| `std.encoding.ebcdic` | CP037 | 95 个可打印字符的固定表；`A`..`I`/`J`..`R`/`S`..`Z` 三段不连续、数字在 `0xF0` 起；编码表外字符写 `0x6F`，解码表外字节报 -1 |

几个数字记一下免得再算：basE91 的 1.23 是**字符/字节**，所以对短 ASCII 文本会膨胀；
uuencode 的 `Hello, uuencode!` 是 **16** 字节（不是 17），行首字符因此是 `0`；
yEnc 的转义集合是**编码后**的值，所以原始 `0xD6`/`0xE0`/`0xE3`/`0x13` 才会被转义。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.base91`=0x60b1、
`std.encoding.uu`=0x60b2、`std.encoding.yenc`=0x60b3、`std.encoding.ebcdic`=0x60b4；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-transcode` 与 `STD_TRANSCODE_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`。
示例 `examples/stdtranscode.c6` 四节（21 条自检）。

验收：`make stdtest`（181 个模块、0 failure）、`make std-transcode`（21 条自检全过）、
`make check-all`。

### 11.55 标准库第四十三批（mp3 / ogg / flac / midi）

第四十三批做音频容器。除了已有的 WAV，这一批补齐「编码流」侧的四种格式：MP3
（帧头 + ID3）、Ogg（页 + 编解码识别）、FLAC（元数据块 + 按位打包的 STREAMINFO）、
MIDI（轨 + 变长量事件）。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.mp3` | ID3 + 帧头 | ID3v2 长度是 synchsafe（每字节只用 7 位）；ID3v1 固定 128 字节在文件尾；帧头的码率表按「MPEG1 / MPEG2·2.5」和层分三套，`free`/`bad` 索引都判坏；Layer III 在 MPEG2 上帧长用 72 而不是 144 |
| `std.data.ogg` | 页 + 流头 | 页长度 = 27 + 段数 + 段表字节和；granule 是 64 位**有符号**（-1 表示无值）；BOS 标志在标志字节的 0x02 位，首包按魔数分派 Vorbis/Opus/Speex/FLAC/Theora |
| `std.data.flac` | STREAMINFO | 采样率/声道/位深/总样本数是**跨字节按位**打包（20+3+5+36 位挤在 8 字节里），要按位读而不是按字节读；元数据块的「最后一块」标志在第 1 字节的 bit7 |
| `std.data.midi` | SMF | delta time 是 1~4 字节变长量（每字节 7 位、最高位是续位）；通道消息按高 4 位决定跟 1 个还是 2 个数据字节（`0xC`/`0xD` 只跟 1 个）；running status 明确不支持 |

坑记一个：MIDI 事件的整段长度必须把 delta 的字节数算进去，否则「事件刚好走完轨」
这类自检会差一个字节——`size` 是「delta 字节数 + 状态与数据」。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.mp3`=0x60b5、
`std.data.ogg`=0x60b6、`std.data.flac`=0x60b7、`std.data.midi`=0x60b8；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-audio` 与 `STD_AUDIO_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`。示例
`examples/stdaudio.c6` 四节（26 条自检）。

验收：`make stdtest`（185 个模块、0 failure）、`make std-audio`（26 条自检全过）、
`make check-all`。

### 11.56 标准库第四十四批（smtp / ftp / redis / telnet）

第四十四批补「文本行协议」：SMTP、FTP、Redis 的 RESP、Telnet。它们的共同点是
「一行一条消息 + 少量二进制控制字节」，正好把行解析与状态机的小把戏收拾干净。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.net.smtp` | 应答 + DATA | 状态码后第 4 个字节是 `-` 表示还有后续行、是空格表示末行；`smtp_data` 要同时做「CRLF 归一」「行首点再补点」「末尾 `.<CRLF>`」三件事，且不能把已是 CRLF 的输入再插一个 `\r` |
| `std.net.ftp` | 应答 + PASV + 列表 | PASV 的端口是 `p1*256+p2`（所以 `10,20` 是 2580）；`LIST` 行第 5 个字段是大小、之后跳过月/日/时间才是文件名，字段数**数错一列**文件名就会带上前面的时间 |
| `std.net.redis` | RESP | 五种前缀各一套语义：`$` 的 `len` 是数据长度、`*` 的 `len` 是元素个数、`-1` 是 null；数组只解头，元素靠 `total` 递进 |
| `std.net.telnet` | IAC | `IAC IAC` 是一个字面 0xFF（不是两个字节的数据）；应答方向是 WILL→DO、DO→WILL、WONT→DONT、DONT→WONT；SB…SE 整段跳过 |

FTP 的解析坑值得记一笔：`LIST` 行是定字段的「权限 链接 属主 属组 大小 月 日 时间
名字」，早期按「循环跳 8 个字段再读大小」写，结果大小读到的位置偏移了一列；改成
「先跳 4 个、读大小、再跳 3 个」就稳了。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.net.smtp`=0x60b9、
`std.net.ftp`=0x60ba、`std.net.redis`=0x60bb、`std.net.telnet`=0x60bc；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-proto2` 与 `STD_PROTO2_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`。示例
`examples/stdproto2.c6` 四节（23 条自检）。

验收：`make stdtest`（189 个模块、0 failure）、`make std-proto2`（23 条自检全过）、
`make check-all`。

### 11.57 标准库第四十五批（fenwick / segtree / dsu / bloom）

第四十五批补数据结构：树状数组、迭代线段树、并查集、布隆过滤器。四个都是
「调用方给缓冲区、模块只管算法」的形态，与 `std.container.region`/`pool` 的
零分配风格一致。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.container.fenwick` | 树状数组 | 对外下标 0 起、内部树 1 起；`fenwick_lower` 用「从最高 2 的幂往下跳」的经典写法找首个前缀和 >= target 的位置，超过总和返回 `n` |
| `std.container.segtree` | 迭代线段树 | 2n 数组，叶子在 `[n,2n)`；聚合方式由 `op` 决定，单位元也跟着变（求和 0、最小 `SEG_INF`、最大 `-SEG_INF`）；`seg_range` 用左右两个累加器再合成，保证非交换聚合也正确 |
| `std.container.dsu` | 并查集 | `dsu_find` 是**迭代**路径减半（不递归，深链也不会爆栈）；`dsu_union` 按集合大小合并并维护 `count` |
| `std.container.bloom` | 布隆过滤器 | FNV-1a 双哈希 + 线性组合 `h1+i*h2`；`count` 记录已置位个数，`bloom_fill_ratio` 给千分数，避开浮点依赖 |

一个容易写错的地方：线段树的 `seg_range` 左右两个累加器合并顺序不同（右边要
`combine(t[b], right)`），写成一样的顺序在求和的树上没问题，换成非交换的聚合就
会错。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.container.fenwick`=0x60bd、
`std.container.segtree`=0x60be、`std.container.dsu`=0x60bf、`std.container.bloom`=0x60c0；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-ds` 与 `STD_DS_SRCS`，`test-all`/`check-all`
都带上它；前三个模块无依赖（各自 2 条用例），`bloom` 依赖 `std/text/str.c6`（24 条）。
示例 `examples/stdstruct.c6` 四节（26 条自检）。

验收：`make stdtest`（193 个模块、0 failure）、`make std-ds`（26 条自检全过）、
`make check-all`。

### 11.58 标准库第四十六批（slug / casing / roman / phonetic）

第四十六批做「文本变换」：slug 化、命名风格互转、罗马数字、发音编码。四个都只
吃字符串、不碰 I/O，属于那种「写起来小、但每个项目都会自己抄一遍」的东西。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.text.slug` | URL slug | 输入按 **UTF-8** 解（先解出码点再判断），U+00C0..U+00FF 用 64 项折叠表映射到基字母；三/四字节过程序当分隔——所以 CJK 整段丢掉、但夹在字母之间仍留下**一次**分隔 |
| `std.text.casing` | 命名风格 | 切词三条规则：非字母数字是分隔、小写/数字接大写是边界、连续大写后跟小写时最后那个大写属于新词；数字不单独切（`utf8String` 是两个词） |
| `std.text.roman` | 罗马数字 | 转换本身很简单，难点是**校验**：先按减法规则算值，再用标准形式写回来逐字符比对，只有完全一致才算合法 |
| `std.text.phonetic` | Soundex / Metaphone | Soundex 里 `h`/`w` 不打断同码、元音才打断；Metaphone 简化版把结果先写进本地缓冲再按 `cap` 判溢出，避免「边写边判容量」时把半截结果留在调用方缓冲里 |

踩到的一个语言层面的坑：模块名不能叫 `std.text.case`（`case` 是关键字，`module`
语句在解析点就报 `module name expected after '.'`），所以文件叫 `casing.c6`、
模块名 `std.text.casing`。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.text.slug`=0x60c1、
`std.text.casing`=0x60c2、`std.text.roman`=0x60c3、`std.text.phonetic`=0x60c4；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-words` 与 `STD_WORDS_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`。
示例 `examples/stdwords.c6` 四节（23 条自检）。

验收：`make stdtest`（197 个模块、0 failure）、`make std-words`（23 条自检全过）、
`make check-all`。

### 11.59 标准库第四十七批（vector / polynomial / fft / spline）

第四十七批补数值工具：矢量、多项式、FFT、曲线插值。这一批开始碰 `libm`，
`stdtest` 里没关系（测试驱动一直带 `-lm`），但拼装示例的目标要在链接行显式加 `-lm`。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.vector` | Vec2 / Vec3 | 值类型、按值传参；归一化遇到零矢量返回零矢量而不是 NaN；`vec2_cross` 返回标量、`vec3_cross` 返回矢量；`vec3_angle` 把 `dot/(|a||b|)` 夹到 `[-1,1]` 再 `acos` |
| `std.core.polynomial` | 多项式 | 系数升幂、`n` 是系数个数；求值用 Horner；加减乘都写到调用方缓冲并 `poly_trim` 去尾零；`poly_roots2` 按判别式返回根数并保证 `x1 <= x2` |
| `std.core.fft` | 基 2 FFT | 原地位反转 + 三层循环蝶形；逆变换带 `1/n` 归一化；**要求长度是 2 的幂**（`fft_ok`），奇数长度直接 -1 而不是偷偷补零 |
| `std.core.spline` | 贝塞尔 / Hermite / Catmull-Rom | 全部是标量函数（多维按分量调用）；Catmull-Rom 的切线取 `(p2-p0)/2`、`(p3-p1)/2`；`hermite` 的四个基函数系数容易抄错，测试里用「两端切线为 0 时中点等于中值」和「只有 m0 时中点 = 0.625*Δ」两种情形卡住 |

模块名踩坑两处：文件必须叫 `polynomial.c6`（`module std.core.polynomial` 的名字就是
路径，写成 `poly.c6` 会报 `module file declares … expected …`）；示例 `import` 的名字
也得跟着改。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.vector`=0x60c5、
`std.core.polynomial`=0x60c6、`std.core.fft`=0x60c7、`std.core.spline`=0x60c8；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-math2` 与 `STD_MATH2_SRCS`（链接带
`-lm`），`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里无依赖、各 2 条用例。
示例 `examples/stdnumeric.c6` 四节（23 条自检）。

验收：`make stdtest`（201 个模块、0 failure）、`make std-math2`（23 条自检全过）、
`make check-all`。

### 11.60 标准库第四十八批（calendar / iso8601 / duration / cron）

第四十八批做时间：历法换算、ISO 8601、时长串与 cron。这批的共同点是「字符串/
数字 ↔ 时刻」的边界特别多，测试全用固定 epoch 钉死，不碰 `time(0)`。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.os.calendar` | 历法换算 | 用 Hinnant 的 `days_from_civil`（把 3 月当一年开头的移位纪年），没有循环、对 1970 前的负数天数也成立；`iso_week` 用「本周的周四」定年份，所以 2023-01-01 会算成 2022-W52 |
| `std.os.iso8601` | ISO 8601 | 解析分三段：日期 → 时间 → 时区；小数秒只保留到毫秒；`iso_to_epoch` 要把 `offset_min` **减掉**（东八区的 08:00 等于 UTC 的 00:00） |
| `std.os.duration` | 时长串 | 单位只在数字后出现，纯数字按秒；小数按单位换算后四舍五入到毫秒；`dur_format` 只输出最大的两个单位，所以 `125000` 是 `2m5s` 而不是 `2m5s0ms` |
| `std.os.cron` | cron | 位图表示（分钟 60 位、小时 24 位、日 32 位、月 13 位、周 8 位）；「日/周都受限时取或」是 cron 的老规矩；`cron_next` 逐分钟扫、上限 6 年，2017 年后不存在的 2 月 30 日会正常返回 0 |

踩到的坑是注释里的 `*/`：文档注释里写 `*/15` 会**提前结束块注释**，编译报
`declaration expected`。改成 `0,15,30,45` 或在注释里用反引号把 `*` 与 `/n` 分开写。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.os.calendar`=0x60c9、
`std.os.iso8601`=0x60ca、`std.os.duration`=0x60cb、`std.os.cron`=0x60cc；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-time` 与 `STD_TIME_SRCS`，`test-all`/`check-all` 都
带上它；`iso8601` 与 `cron` 在 `stdtest` 里依赖 `std/text/str.c6` + `std/os/calendar.c6`
（各 26 条用例），`calendar`/`duration` 无依赖（各 2 条）。示例 `examples/stdtime.c6`
四节（27 条自检）。

验收：`make stdtest`（205 个模块、0 failure）、`make std-time`（27 条自检全过）、
`make check-all`。

### 11.61 标准库第四十九批（ntp / syslog / stun / tftp）

第四十九批做网络报文：NTP、syslog、STUN、TFTP。四个都是「二进制或半文本报文 +
少量状态」，与前面 ws/mqtt/socks5 的做法一致，只解报文、不做 I/O。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.net.ntp` | NTPv4 | 时间戳是 1900 纪元的 32.32 定点数，与 Unix 差 2208988800 秒；`ntp_offset` 用 RFC 5905 的 `((t2-t1)+(t3-t4))/2`，测试里用「服务器快 5 秒、单程 1 秒」的对称模型钉死 |
| `std.net.syslog` | RFC 5424 / 3164 | 先用 `<PRI>` 后的第一个字节区分：数字是 5424 的 VERSION，字母是 3164 的月份；5424 的 `-` 表示「没有这个字段」，要归一成长度 0；3164 的 `TAG: MSG` 再把 TAG 拆出来 |
| `std.net.stun` | RFC 5389 | 类型字段是**位交错**的（方法位与类别位穿插），用 `stun_make_type` 统一算；属性按 4 字节补齐，遍历时步进是 `4 + ((len+3)&~3)`；XOR-MAPPED 的端口异或魔术字高 16 位、IPv4 地址异或整个魔术字 |
| `std.net.tftp` | RFC 1350 | 前 2 字节是操作码，其后按操作码分支；RRQ/WRQ 是一串 ` ` 结尾的串（文件名、模式、选项对），DATA 是「块号 + 裸数据」，所以 `data_off` 恒为 4 |

两个小坑：`str` 的切片字段 `len` 是只读的（`f->s.len = 0` 会报警告
`assignment of read-only location`），要清空就把整个切片赋成零长；测试里别把变量
命名成 `err`（它是内置的错误构造），`static const u8 err[...]` 会直接编译失败。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.net.ntp`=0x60cd、
`std.net.syslog`=0x60ce、`std.net.stun`=0x60cf、`std.net.tftp`=0x60d0；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-packet` 与 `STD_PACKET_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`（各 24 条用例）。示例
`examples/stdpacket.c6` 四节（29 条自检）。

验收：`make stdtest`（209 个模块、0 failure）、`make std-packet`（29 条自检全过）、
`make check-all`。

### 11.62 标准库第五十批（log / sqlite / postgres / mysql）

第五十批按「日志 + 数据库」补齐：定长环形日志、SQLite 文件结构、PostgreSQL 与
MySQL 的前端报文。数据库这三个都只做**报文/页结构**，不连库、不发 SQL。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.os.log` | 环形日志 | 记录是定长槽位（`scope` 24、`msg` 96），不分配不落盘；`max_level` 以上才写，`log_at(0)` 是最新一条；`log_dump` 从最旧的可用记录开始；覆盖与过滤分别计数 |
| `std.data.sqlite` | 文件 + B-tree 页 | 页大小字段值为 1 表示 65536；页类型只认 2/5/10/13；单元按类型分别解 rowid（表页才有）；本地 payload 长度用 SQLite 的 `maxLocal/minLocal` 公式，超出时置 `overflow` 但不跟溢出页 |
| `std.net.postgres` | v3 报文 | 普通报文长度字段**含自身 4 字节、不含类型字节**；启动包反过来（没有类型字节、长度含自身）；启动参数是 `键\0值\0`、最后再来一个 `\0` |
| `std.net.mysql` | 报文 + 握手 | 报文长度是 3 字节**小端**（与 MySQL 里几乎所有的多字节整数一致）；握手包的能力位分低/高两段，挑战串也是两段（8 + 至多 13），合计 20 字节（第 21 个是结尾 NUL） |

踩坑：`Str` 只有 `s` 一个切片字段，取长度要写 `v.s.len`（直接写 `v.len` 会报
`field not found`）；切片字段的 `len` 是只读的，想「清空」只能整体赋成零长切片；
另外变量别叫 `err`（内置构造），`static u8 err[64]` 会直接编译失败。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.os.log`=0x60d1、
`std.data.sqlite`=0x60d2、`std.net.postgres`=0x60d3、`std.net.mysql`=0x60d4；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-db` 与 `STD_DB_SRCS`，`test-all`/
`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（log/postgres/mysql 各 24 条，sqlite 25 条）。示例 `examples/stddb.c6` 四节
（30 条自检）。

验收：`make stdtest`（213 个模块、0 failure）、`make std-db`（30 条自检全过）、
`make check-all`。

### 11.63 标准库第五十一批（fnv / adler32 / murmur3 / siphash）

第五十一批把「非加密散列」补齐：FNV、Adler-32、MurmurHash3（32/128）、SipHash。
与已有的 sha 家族、blake2s、crc32c、xxhash 一起，散列这块基本齐了。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.fnv` | FNV-1/FNV-1a | 两种变体只差「先异或再乘」还是「先乘再异或」；`Fnv.bits` 用 32/33、64/65 编码变体，`fnv_update` 按它分派；空输入就是偏移基数 |
| `std.encoding.adler32` | Adler-32 | 每 5552 字节取一次模（65521 下的经典分块值，避免 `b` 溢出）；`adler32_combine` 用 `b = b1 + b2 + len2*a1 - len2` 的公式，合并结果与整体一致 |
| `std.encoding.murmur3` | MurmurHash3 | 32 位是 x86 变体（尾部 3/2/1 字节）、128 位是 **x64 变体**（两个 64 位状态、两个常量），别把两者的常量搞混——x86_128 的四个 32 位常量是另一套 |
| `std.encoding.siphash` | SipHash | 官方测试向量必须过：密钥 `00..0f`、空串 `0x726FDB47DD0E0E31`、15 字节 `0xA129CA6149BE45E5`；流式状态把「不满 8 字节的尾巴」存在 `buf` + `buf_len` 里，长度字节在收尾时按 `total & 0xFF` 塞进最高位 |

写这批时踩了两个 c6 的坑：**注释里不能再出现 `*/`**（`unsigned char key[16] = {…};
/* 共享密钥 */` 会把块注释提前结束，报 `declaration expected`）；`slice` 的 `.data`
是只读的，`data.data += chunk` 会警告 `assignment of read-only location`，要改成
索引偏移。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.fnv`=0x60d5、
`std.encoding.adler32`=0x60d6、`std.encoding.murmur3`=0x60d7、`std.encoding.siphash`=0x60d8；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-hash2` 与 `STD_HASH2_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条用例）。示例 `examples/stdhash2.c6` 四节（20 条自检）。

验收：`make stdtest`（217 个模块、0 failure）、`make std-hash2`（20 条自检全过）、
`make check-all`。

### 11.64 标准库第五十二批（treap / heap / skiplist / counter）

第五十二批补「有序容器」：Treap、二叉堆、跳表、频次计数器。四个都是调用方给数组、
模块只管算法的形态，与前面 fenwick/segtree/dsu 一致。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.container.treap` | 随机化 BST | 用 `split`/`merge` 实现插入删除；`size` 字段顺带支持 `rank`（严格小于）与 `kth`（第 k 小）；节点槽位**不回收**（`used` 与 `n` 分开），否则删掉的槽位会被新节点复用、把老树指针踩坏 |
| `std.container.heap` | 二叉堆 | 最小/最大共用一套 `heap_better`；`heap_build` 从 `n/2-1` 往下沉是 O(n)；`heap_pushpop` 的语义是「压入后立刻弹出」，所以 `v` 更优先时返回 `v` 自己、堆不变 |
| `std.container.skiplist` | 跳表 | `nodes[0]` 是头哨兵；每层找前驱后统一改链；层数按 `(rand & 3) == 0` 几何增长、上限 8 层；`skip_next` 用「key+1 的前驱」实现严格大于 |
| `std.container.counter` | 频次计数 | 键定长 23 字节（拷贝进槽位，没有生命周期问题）；开放寻址 + 线性探测，装到 3/4 返回满；`counter_top` 是线性扫一遍（没有维护堆，够用且简单） |

Treap 那个「槽位不回收」的坑值得记：一开始用 `t->n--` 同时表示「元素数减少」，结果
新插入会覆盖 `nodes[n]`——而那个节点可能还挂在树上，`rank`/`kth` 立刻算错。把
「分配游标 `used`」和「元素数 `n`」拆成两个字段就解决了。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.container.treap`=0x60d9、
`std.container.heap`=0x60da、`std.container.skiplist`=0x60db、`std.container.counter`=0x60dc；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-ordered` 与 `STD_ORDERED_SRCS`，
`test-all`/`check-all` 都带上它；`counter` 依赖 `std/text/str.c6`（24 条用例），其余
三个无依赖（各 2 条）。示例 `examples/stdordered.c6` 四节（26 条自检）。

验收：`make stdtest`（221 个模块、0 failure）、`make std-ordered`（26 条自检全过）、
`make check-all`。

### 11.65 标准库第五十三批（bwt / mtf / delta / huffman）

第五十三批做「变换式压缩积木」：BWT、MTF、差分、规范 Huffman。前三节正好是 bzip2
的前半段（BWT → MTF → 熵编码），Huffman 是最后那一步的教科书实现。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.bwt` | Burrows-Wheeler | 排循环移位用「倍增 + 计数排序」（O(n log n)），等价类 `c[i]` 表示以 i 开头的 2^h 长子串名次；逆变换用 LF 映射一遍 O(n) 还原。临时数组是模块静态缓冲（上限 65536），所以不可重入 |
| `std.encoding.mtf` | Move-To-Front | 256 项定长符号表，输出位置后把符号移到表头；连续相同字节第一个输出其初值、其余全是 0 |
| `std.encoding.delta` | 差分 | 字节版按模 256 差分（递减时是 0xFF），64 位版直接相减；一字节错会传播到结尾，文档里写清 |
| `std.encoding.huffman` | 规范 Huffman | 先建树拿码长、再按 (码长, 符号) 分配规范码；**单符号要特判**（长度 1 的码 Kraft 等式不成立，但这是合法输入）；解码逐位累积 + 线性找同长度码，简单但慢 |

两个小坑：MTF 示例里 200 字节塞进 64 字节缓冲会静默踩坏相邻变量（表现为「0 的
个数不对」），加缓冲断言/放大数组即可；BWT 的"主行号"必须在 `[0, n)`，逆变换
对越界值返回 -1。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.bwt`=0x60dd、
`std.encoding.mtf`=0x60de、`std.encoding.delta`=0x60df、`std.encoding.huffman`=0x60e0；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-bwt` 与 `STD_BWT_SRCS`，`test-all`/
`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`（各 24 条）。
示例 `examples/stdcmp.c6` 四节（22 条自检）。

验收：`make stdtest`（225 个模块、0 failure）、`make std-bwt`（22 条自检全过）、
`make check-all`。

### 11.66 标准库第五十四批（srt / ass / lrc / m3u）与「方法化」整理

第五十四批做「字幕与播放列表」：SubRip/WebVTT、ASS、LRC、M3U。四种都是「一行一条
记录」的文本格式，正好统一成**游标 + 方法**的用法：

```c
SrtDoc doc = { 0 };
SrtCue cue = { 0 };
doc.open(buf, n);                 // 打开并计数
while (doc.next(&cue) == 1)       // 逐条
    …
doc.seek(3000);                   // 跳到第一条 start_ms >= 3000
doc.rewind();                     // 复位
```

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.srt` | SubRip / WebVTT | `srt_parse` 从任意偏移解一条（跳过序号行、`WEBVTT` 头、`NOTE` 块）；WebVTT 的 cue 标识行会让「时间行」晚一行，所以要「找不到 `-->` 就再读一行」；时间解析同时认 `,` 与 `.`，并修正了「1~2 位小数被多除一次」的老 bug |
| `std.data.ass` | ASS/SSA | 事件字段按**位置**取，但 Text 是最后一段、要取到行尾（否则带逗号的台词会被截断）；PlayResX/Y 的数字前要跳过空格 |
| `std.data.lrc` | LRC | `[offset:]` 会加到每条时间上；`tag()` 用「`[name:` 前缀 + 末尾 `]`」的规则取元信息；格式化分钟至少两位（`[01:02.50]`） |
| `std.data.m3u` | M3U/M3U8 | `#EXTINF` 的秒换算成毫秒并保留标题，`#EXT-X-` 出现即视为 HLS；没有 `#EXTINF` 的条目 `duration_ms` 为 -1 |

同时把前面几批的「状态型」模块统一成**方法优先**的形态（用户要求「充分使用结构体
方法」）：`fenwick`、`segtree`、`dsu`、`bloom`、`log`、`treap`、`heap`、`skiplist`、
`counter` 的函数都改成了裸方法名，调用统一写成 `f.add(...)`、`h.push(...)`、
`t.put(...)`、`lg.dump(...)`，`test` 块与 `examples/stdstruct.c6`、
`examples/stdordered.c6`、`examples/stddb.c6` 也跟着改了。

c6 的方法规则这里踩了三个坑：

1. **方法名就是函数名**：`push(Ring *r, …)` 才能写成 `r.push(…)`；`ring_push` 只会
   得到 `r.ring_push(…)`。所以方法化必须真的改函数名。
2. **字段不能与同名方法共存**：`Dsu.count`→`nsets`、`Bloom.bits`→`set_bits()`、
   `Log.filtered/dropped`→`nfilt/ndrop`、`Counter.total`→`sum()`，都是先撞上
   `function pointer expected`/`field not found` 才发现的。
3. **首参是结构体指针的函数必须用方法语法调用**：留着 `count(&d)` 这种写法会报
   `method 'count' must be called`。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.srt`=0x60e1、
`std.data.ass`=0x60e2、`std.data.lrc`=0x60e3、`std.data.m3u`=0x60e4；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-subs` 与 `STD_SUBS_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里都依赖 `std/text/str.c6`（srt 26、ass 25、
lrc 24、m3u 24 条）。示例 `examples/stdsubs.c6` 四节（22 条自检，全部方法调用）。

验收：`make stdtest`（229 个模块、0 failure）、`make std-subs`（22 条自检全过）、
`make check-all`。

### 11.67 标准库第五十五批（netrc / sshconfig / keys / crontab）

第五十五批做「凭据与配置」：`.netrc`、OpenSSH 配置、`authorized_keys` 行与 crontab。
四种都是「一行一条记录 + 少量缩进/引号」的文本格式，延续上一批的**游标方法**形态：

```c
SshDoc doc = { 0 };
SshBlock blk = { 0 };
Str user = { 0 };
doc.open(buf, n);
doc.match(S("web"), &blk);              // 按 Host 模式匹配
blk.get(buf, n, "user", &user);        // 块内取键（大小写不敏感）
```

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.netrc` | 凭据文件 | `next` 用「读到下一个 `machine` 关键字就把游标退回该关键字之前」的办法分割条目；`#` 注释在行内也生效（`password pb # 备注`）；`macdef` 正文跳到空行 |
| `std.data.sshconfig` | SSH 配置 | 关键字匹配要**跳过行首缩进**（`    User deploy`），所以抽了 `ssh_keyword_at` 返回「值从哪开始」；模式匹配支持空格分隔多模式、`!` 排除与 `*`/`?`（`glob_match` 是双指针回溯版本） |
| `std.data.keys` | 公钥行 | `command="echo hi"` 里的空格不能当分隔，所以词法要跟踪双引号；类型位数按名字给（`ecdsa-*-384` 之类） |
| `std.data.crontab` | crontab | 定时行与环境变量共用一个文件、但用**两个游标**（`off`/`voff`）；`@reboot` 之类别名整行进 `command` 并置 `alias`；字段不足返回 -1 |

又踩了一次「注释里出现 `*/`」：crontab 文档里写 `*/5 * * * *` 会把块注释提前结束，
改成反引号包起来的 `*` `/5`。另外 c6 要求「首参是结构体指针的函数必须用方法语法
调用」，所以 `open` 里想复用 `next` 计数时要注意**定义顺序**（`next` 必须在
`open` 之前），否则报 `too many arguments to function`。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.netrc`=0x60e5、
`std.data.sshconfig`=0x60e6、`std.data.keys`=0x60e7、`std.data.crontab`=0x60e8；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-creds` 与 `STD_CREDS_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条用例）。示例 `examples/stdcreds.c6` 四节（20 条自检，全部方法调用）。

验收：`make stdtest`（233 个模块、0 failure）、`make std-creds`（20 条自检全过）、
`make check-all`。

### 11.68 标准库第五十六批（geo / geohash / maidenhead / dms）

第五十六批做「地理编码」：球面几何、Geohash、Maidenhead 网格与度分秒。这批开始用
`libm`（`sin/cos/asin/atan2/sqrt`），所以拼装示例的链接行要带 `-lm`。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.geo` | 球面几何 | 距离用 haversine、方位用 `atan2` 初始方位、目的地用球面正解；`geo_bbox` 用「半径 / 地球半径」换纬度差、再除以 `cos(lat)` 换经度差，跨 ±180 时 `min_lon > max_lon`（`GeoBox.contains` 内部按跨日界线判断） |
| `std.data.geohash` | Geohash | 编码时位从**经度**开始交替；解码用「逐位二分」重建格子；`neighbor` 是「解出中心 + 按格子大小平移 + 重新编码」，跨日界线靠 `geo_point` 归一化 |
| `std.data.maidenhead` | Maidenhead | 四级（20°×10° → 2°×1° → 5′×2.5′ → 0.5′×0.25′），字符级与数字级交替，所以 `precision` 必须成对（2/4/6/8） |
| `std.data.dms` | 度分秒 | 解析用一个「分隔符 + 可选方向字母 + 数字」的循环，遇到第一个带小数的数就直接按十进制度处理；`format` 的度符号写成 UTF-8 的两字节 `0xC2 0xB0` |

一个容易忽略的点：`grid_valid` 最初按「字符位置奇偶」判字母/数字，结果 `FN31pr`
被拒——正确的是按**第几对**判（第 0/2 对是字母、第 1/3 对是数字）。另外 geohash
的「北京 6 位」是 `wx4g0b`（不是常被记成的 `wx4g0e`），测试向量按参考实现重算过。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.geo`=0x60e9、
`std.data.geohash`=0x60ea、`std.data.maidenhead`=0x60eb、`std.data.dms`=0x60ec；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-geo` 与 `STD_GEO_SRCS`（带 `-lm`），
`test-all`/`check-all` 都带上它；`geohash`/`maidenhead` 在 `stdtest` 里依赖
`std/text/str.c6` + `std/core/geo.c6`（各 26 条），`dms` 依赖 str（24 条），`geo`
无依赖（2 条）。示例 `examples/stdgeo.c6` 四节（30 条自检）。

验收：`make stdtest`（237 个模块、0 failure）、`make std-geo`（30 条自检全过）、
`make check-all`。

### 11.69 标准库第五十七批（ar / coff / class / rpm）

第五十七批做「目标文件与包」：`ar` 归档、MS COFF 目标文件、Java `.class`、RPM 包头。
四个都是「固定头 + 索引表 + 变长名字」的结构，统一成**游标方法**：

```c
ArDoc doc = { 0 };
ArMember m = { 0 };
doc.open(buf, n);
while (doc.next(&m) == 1)
    …
doc.find(S("util.o"), &m);
```

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.ar` | Unix 归档 | 名字四种形态都要认：`name/`、`/`（符号表）、`//`（字符串表）、`/123`（长名偏移）与 BSD `#1/N`（名字在数据开头）；`next` 里跳过特殊成员后**必须推进游标**，否则会原地打转（这批踩到的死循环） |
| `std.data.coff` | MS COFF | 20 字节头 + 40 字节节头 + 18 字节符号；节名 `/<digits>` 与符号名（前 4 字节为 0 时后 4 字节是字符串表偏移）都要查字符串表 |
| `std.data.class` | Java class | 常量池 14 种 tag 的长度表，`Long`/`Double` 占**两个槽位**（遍历时 `i++` 两次）；方法要跨过字段的属性表才能定位，名字/描述符是 `Utf8` 项的视图 |
| `std.data.rpm` | RPM | lead（96 字节）与主头（16 字节 + `nindex × 16`）都是大端；索引项给 tag/type/offset/count，字符串取到 `\0` 为止，整数按大端解 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.ar`=0x60ed、
`std.data.coff`=0x60ee、`std.data.class`=0x60ef、`std.data.rpm`=0x60f0；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-pkg` 与 `STD_PKG_SRCS`，`test-all`/`check-all` 都带上
它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`（各 24 条用例）。示例
`examples/stdpkg.c6` 四节（22 条自检，全部方法调用）。

验收：`make stdtest`（241 个模块、0 failure）、`make std-pkg`（22 条自检全过）、
`make check-all`。

### 11.70 标准库第五十八批（luhn / crc16 / hamming / classic）

第五十八批做「校验与纠错」：校验位（Luhn / ISBN / Verhoeff）、CRC-8/16、汉明码 (7,4)
与几个经典替换密码。`Crc` 与 `HammingBlock` 都是**方法优先**的形态。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.encoding.luhn` | 校验位 | Luhn 从右往左按「偶数位翻倍」，`luhn_digit` 要把主体当成「校验位在最右」反着算；ISBN-10 的校验位可能是 `X`(10)；Verhoeff 用 d/p/inv 三张表，校验位的下标要从 1 起算 |
| `std.encoding.crc16` | CRC-8/16 | 五种参数（多项式/初值/是否反射）用一张 `init` 分派表；反射版多项式写成反向形式（0xA001），`update` 按低位先处理；`"123456789"` 的五个标准检查值全对上 |
| `std.encoding.hamming` | 汉明 (7,4) | 位置 1/2/4 是校验位、3/5/6/7 是数据位；校验子直接给出出错位置（1..7），异或那一位即可纠正；码距 3 只能纠 1 位错 |
| `std.encoding.classic` | 经典密码 | 只动 ASCII 字母（大小写保持），维吉尼亚的 key 跳过非字母、`decrypt` 做减法；栅栏密码的编码/解码都用「每栏读一遍」的走法 |

又踩了字段与同名方法的冲突：`Crc.width` 与 `width()` 方法撞车（`function pointer
expected`），把字段全改成带后缀的名字（`wbits`/`initv`/`polyn`/`refl`/`kindc`），
`width()` 方法改名 `bits()`。这类冲突现在已经有固定套路：**字段加后缀、方法用动词**。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.encoding.luhn`=0x60f1、
`std.encoding.crc16`=0x60f2、`std.encoding.hamming`=0x60f3、`std.encoding.classic`=0x60f4；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-check` 与 `STD_CHECK_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条用例）。示例 `examples/stdcheck.c6` 四节（28 条自检）。

验收：`make stdtest`（245 个模块、0 failure）、`make std-check`（28 条自检全过）、
`make check-all`。

### 11.71 标准库第五十九批（mounts / fstab / osrelease / systemd）

第五十九批做「Linux 配置文本」：`/proc/mounts`、`/etc/fstab`、`/etc/os-release` 与
systemd unit 文件。四个都是「一行一条记录 + 注释」的文本，统一成游标方法。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.os.mounts` | /proc/mounts | 固定 6 字段；挂载点里的空格写成 `\040`，所以提供 `unescape`（`\040`/`\011`/`\012`/`\134`）；字段是原串视图，要「人能读」的路径得自己转 |
| `std.os.fstab` | /etc/fstab | 4~6 字段（dump/passno 可省，默认 0）；`#` 注释与空行跳过；设备可以是 `UUID=`/`LABEL=`；多于 6 个字段的部分忽略 |
| `std.os.osrelease` | os-release | `KEY=VALUE`，值两侧引号成对时剥离，`#` 注释只在行首生效（引号里的 `#` 是值的一部分）；没有 `=` 的行跳过而不是报错 |
| `std.os.systemd` | unit 文件 | `[Section]` 与 `Key=Value`；只按**第一个** `=` 切分（`Environment=A=1 B=2` 的值要完整）；注释支持 `#` 与 `;`；同名段出现两次会各计一次 |

这批又踩了两次同类坑，值得记进「套路」：一是**游标推进**——`next` 里凡是 `continue`
的分支都必须保证 `d->off` 已经前进（mounts 第一版在注释行上原地打转，表现为测试
挂死）；二是**切片字段只读**——`osr_trim(Str *s)` 里改 `s->s.data`/`s->s.len` 只会
拿到 `assignment of read-only location` 警告且不生效，正确做法是返回一个新的 `Str`。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.os.mounts`=0x60f5、
`std.os.fstab`=0x60f6、`std.os.osrelease`=0x60f7、`std.os.systemd`=0x60f8；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-linux` 与 `STD_LINUX_SRCS`，`test-all`/`check-all` 都
带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`（各 24 条用例）。示例
`examples/stdlinux.c6` 四节（20 条自检）。

验收：`make stdtest`（249 个模块、0 failure）、`make std-linux`（20 条自检全过）、
`make check-all`。

### 11.72 标准库第六十批（binio / bitio / half / align）

第六十批补「二进制与数值工具」：整数读写游标、按位读写游标、半精度浮点与对齐工具。
前三个都是**方法优先**的状态结构（`Reader`/`Writer`/`BitReader`/`BitWriter`/`Half`）。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.binio` | 整数读写 | `Reader`/`Writer` 都是「指针 + 容量 + 偏移 + ok」，越界只置 `ok=0`（返回 0/-1）不抛错，调用方在序列末尾统一 `ok()`；LE/BE 各一套，字节序显式 |
| `std.core.bitio` | 按位读写 | 高位在前（MSB-first）；`read` 一次最多 32 位；`align` 读时丢弃剩余位、写时补零 |
| `std.core.half` | fp16 / bf16 | fp16 要处理次正规数（`2^-24` 步长）、`±0`（用 `1/d` 的符号判断）、`±inf`/`NaN`；bf16 是 **fp32** 的高 16 位（不是 double 的高 16 位，第一版写错成后者，1.0 得到 0x3FF0 而不是 0x3F80） |
| `std.core.align` | 对齐工具 | 非 2 的幂原样返回（不报错，避免调用方到处判断）；`next_pow2` 在 `2^63` 处返回 0 表示溢出；`round_up` 支持任意倍数 |

两个新踩的坑：`union` 变量在 c6 里**必须初始化**（`} u = { 0 };`）；同一模块里
**可以同名重载**方法（`bind(Reader *)` 与 `bind(Writer *)` 并存，`r.bind()`/`w.bind()`
按接收者类型分派），但用普通调用语法 `bind(&w, …)` 会挑错版本并报警告——所以调用
统一写成方法形式，这也正好符合本轮的「方法优先」要求。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.binio`=0x60f9、
`std.core.bitio`=0x60fa、`std.core.half`=0x60fb、`std.core.align`=0x60fc；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-bin2` 与 `STD_BIN2_SRCS`（带 `-lm`），
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条用例）。示例 `examples/stdbin2.c6` 四节（25 条自检）。

验收：`make stdtest`（253 个模块、0 failure）、`make std-bin2`（25 条自检全过）、
`make check-all`。

### 11.73 标准库第六十一批（sanitize / tabs / quote / nato）

第六十一批收尾「文本清洗与标注」：清洗器、制表符展开、shell 引用与 NATO 拼读。
四个模块都提供了带开关的结构体，并用方法暴露（`o.apply`、`t.expand`、`q.quote`、
`o.spell`）。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.text.sanitize` | 清洗 | 四项开关独立：去控制符、CRLF→LF、折叠空格/制表符、去两端空白。折叠**不跨行**（换行保留），否则会把多行文本压成一行；`is_clean` 用逐字节规则判断，和 `apply` 的默认规则保持一致 |
| `std.text.tabs` | 制表符 | 展开按「下一个制表位」补空格、换行清零列号；还原只在**行首空白**处替换（不做 `unexpand -a`），避免把单词之间的对齐空格误变成 tab |
| `std.text.quote` | shell 引用 | 单引号包裹 + 内部 `'` 展开成 `'\''`；`unquote` 里单引号内的反斜杠保持字面，双引号外 `\x` 一律解成 `x`（第一版只认几个特定字符，`a\ b` 解不回来） |
| `std.text.nato` | 拼读 | 26 个词 + 数字表；`decode` 大小写不敏感、允许 `Xray` 与 `X-ray` 两种写法（比较时跳过连字符），无法识别的词跳过而不是报错 |

这一批的坑还是「字段与同名方法」：`TabStops.width` 与 `width()` 撞车，字段改名 `w`；
另外 `SanOpts` 的默认值用 `set_defaults()` 方法给，避免结构体字段顺序写错。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.text.sanitize`=0x60fd、
`std.text.tabs`=0x60fe、`std.text.quote`=0x60ff、`std.text.nato`=0x6100；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-clean` 与 `STD_CLEAN_SRCS`，`test-all`/`check-all` 都
带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`（各 24 条用例）。示例
`examples/stdclean.c6` 四节（21 条自检）。

验收：`make stdtest`（257 个模块、0 failure）、`make std-clean`（21 条自检全过）、
`make check-all`。

### 11.74 标准库第六十二批（color / palette / colorname / units）

第六十二批做「颜色与单位」：颜色值与 HSL/HSV 互转、GIMP 调色板、CSS 命名颜色与常用
单位换算。`Color` 与 `Conv` 都是方法优先的小结构。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.color` | 颜色 | `from_hex` 认 `#RGB`/`#RRGGBB`/`#RRGGBBAA`、`0x` 前缀与无前缀；HSL/HSV 都做成双向；`luminance` 用 sRGB 近似的 `x^3`（不用 libm）算相对亮度，`contrast` 给 WCAG 比值；`lerp` 把 `t` 夹到 0..1 |
| `std.data.palette` | 调色板 | GIMP `.gpl` 的 `Name:`/`Columns:` 行要**跳过而不是报错**（第一版当成坏数据直接返回 -1）；简单格式里 `#` 后面不是十六进制数字就当注释 |
| `std.text.colorname` | 命名颜色 | 名字比较忽略大小写与空格/连字符（`alice blue` = `AliceBlue`）；`nearest` 用 RGB 差的平方和；表里**不能有重复颜色**，否则「反查自己」会命中前一个（cyan/aqua、magenta/fuchsia 都踩过，改成 darkcyan/seagreen） |
| `std.core.units` | 单位换算 | 长度/质量/字节走「基准单位倍率」，温度走单独公式（C↔F↔K）；`parse_unit` 大小写不敏感；类别与单位码不匹配返回 -1 |

一个跨模块的命名坑：`std.text.colorname` 里的 `count()` 与 `std.text.str` 的
`count(Str *)` 在同时 import 时冲突（编译报 `too few arguments`），改名为
`color_count()`/`color_at()` 后解决——**导出到公共命名空间的自由函数要用模块前缀**。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.color`=0x6101、
`std.data.palette`=0x6102、`std.text.colorname`=0x6103、`std.core.units`=0x6104；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-color` 与 `STD_COLOR_SRCS`，
`test-all`/`check-all` 都带上它；`palette`/`colorname` 在 `stdtest` 里依赖
`std/text/str.c6` + `std/core/color.c6`（各 26 条），`color`/`units` 依赖 str（各 24 条）。
示例 `examples/stdcolor.c6` 四节（26 条自检）。

验收：`make stdtest`（261 个模块、0 failure）、`make std-color`（26 条自检全过）、
`make check-all`。

### 11.75 标准库第六十三批（stack / queue / slots / bag）

第六十三批补最基础的四件套：栈、环形队列、位图槽位分配器与 `i64` 多重集合。四个都是
「调用方给缓冲区 + 方法操作状态」的形态，与 ring/deque 一脉相承。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.container.stack` | 栈 | 纯视图（`buf` 由调用方给），满时 `push` 返回 0 不覆盖；`bind` 会清零缓冲区，避免读到旧值 |
| `std.container.queue` | 环形队列 | `(head + n) % cap` 定位尾巴，回绕之后出队顺序必须仍然是 FIFO（用例专门验证 3,4,10,11） |
| `std.container.slots` | 槽位分配器 | 一个 bit 一个槽位；`alloc_contig(k)` 找连续空位；越界 `is_used` 返回 1（当占用）而不是 0，避免调用方把无效句柄当空闲 |
| `std.container.bag` | 多重集合 | 开放寻址 + 线性探测；删到 0 时留**墓碑**（`used = 2`）而不是清空，否则同簇的其他键会查不到；装载率 3/4 后只允许已有键累加 |

这批的坑还是字段与同名方法：`Bag.total` 与 `total()` 撞车，字段改成 `sumv`
（`Slots`/`Stack`/`Queue` 的字段名一开始就避开了 `size`/`count`/`total` 这些方法名）。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.container.stack`=0x6105、
`std.container.queue`=0x6106、`std.container.slots`=0x6107、`std.container.bag`=0x6108；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-cont` 与 `STD_CONT_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条用例）。示例 `examples/stdcontainers.c6` 四节（22 条自检）。

验收：`make stdtest`（265 个模块、0 failure）、`make std-cont`（22 条自检全过）、
`make check-all`。

### 11.76 标准库第六十四批（histogram / moving / series / quantile）

第六十四批补「统计小工具」：等宽直方图、滑动平均（SMA/EMA）、序列统计与分位数。
四个都是「调用方给缓冲区/数组 + 方法读状态」的形态。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.histogram` | 直方图 | 等宽分箱，越界值夹到首尾箱；`mean` 用箱中心近似（文档写明「夹断会有偏」）；`mode` 返回箱的左右边界而不是箱号，调用方更好用 |
| `std.core.moving` | 滑动平均 | 环形缓冲 + 常驻 `sum` → `push` 是 O(1)；EMA 是独立累加器、首样本直接作初值；`alpha` 夹到 0..1，`alpha=0` 时 EMA 不变 |
| `std.core.series` | 序列统计 | 方差用样本公式（n-1）；`cov`/`corr` 长度不等时按前 n 个**配对**，均值也只取这 n 个（第一版用整段均值，结果和配对样本不自洽，测试里 4 vs 2 才发现） |
| `std.core.quantile` | 分位数 | 线性插值定义与 NumPy 默认一致；`quantile` **就地排序**（文档强调要保留顺序先复制）；插入排序只适合小数据集 |

字段与同名方法的老问题又出现两次：`Hist.total` → `n`、`Ma.ema` → `emav`。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.histogram`=0x6109、
`std.core.moving`=0x610a、`std.core.series`=0x610b、`std.core.quantile`=0x610c；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-stats` 与 `STD_STATS2_SRCS`（带 `-lm`），
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条用例）。示例 `examples/stdstats2.c6` 四节（21 条自检）。

验收：`make stdtest`（269 个模块、0 failure）、`make std-stats`（21 条自检全过）、
`make check-all`。

### 11.77 标准库第六十五批（kmp / bmh / suffix / wordplay）

第六十五批做「字符串算法」：KMP、BMH、后缀数组与回文/异位词。前两个与后缀数组都是
「模式/文本 + 调用方给数组」的形态，`Ana` 则是 26 字母计数指纹的小结构。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.text.kmp` | KMP | 失败函数由调用方给数组（容量不足返回 -1）；`count` 数**可重叠**出现（`abab` 在 `ababab` 里 2 次），`find` 不重叠语义自然成立 |
| `std.text.bmh` | BMH | 坏字符表只填 0..m-2 的**最后**位置（`needle` → n5 e3 d2 l1），`count` 用「匹配后整体跳过 m」实现不重叠计数 |
| `std.text.suffix` | 后缀数组 | 第一版写的倍增+计数排序有 bug（`banana` 的 `na`/`nana` 顺序反了，LCP 也跟着错），改成「插入排序直接比较后缀」后正确；`search` 二分到左边界后要**向后扫同一前缀段**并取最小起始下标，否则 `ana` 会返回 3 而不是 1 |
| `std.text.wordplay` | 回文/异位词 | 回文判断跳过非字母数字；`longest_palindrome` 中心扩展返回**最早**的最长回文；`Ana.canonical` 输出 `e1i1l1n1s1t1` 形式的指纹 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.text.kmp`=0x610d、
`std.text.bmh`=0x610e、`std.text.suffix`=0x610f、`std.text.wordplay`=0x6110；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-search` 与 `STD_SEARCH_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条用例）。示例 `examples/stdsearch.c6` 四节（22 条自检）。

验收：`make stdtest`（273 个模块、0 failure）、`make std-search`（22 条自检全过）、
`make check-all`。

### 11.78 标准库第六十六批（aiff / au / voc / ircam）

第六十六批做「老音频格式头」：AIFF、Sun `.au`、Creative Voice 与 IRCAM。四种都是
「固定头 + 块/编码号」的结构，AIFF 与 VOC 用块游标，AU 与 IRCAM 用带方法的小结构。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.aiff` | AIFF/AIFF-C | `COMM` 里的采样率是 **80 位扩展浮点**（符号+15 位指数+64 位尾数，值 = M·2^(E−16383−63)）；第一版把指数算错成 `E = 16383+63−shift`，44100 解出来大得离谱，改成 `E = 16383 + bitpos` 才对；块长度奇数要跳 1 字节 |
| `std.data.au` | Sun `.au` | 24 字节大端头；编码号 1..7 与 27（A-law），`bytes_per_sample` 做映射；数据偏移 < 24 判坏 |
| `std.data.voc` | Creative Voice | 1 字节类型 + **3 字节小端**长度；采样率来自块 1（`1000000/(256-rate)`）或块 9（32 位率），块 9 的率字段为 0 时要**沿用旧值**而不是清零 |
| `std.data.ircam` | IRCAM `SF` | 魔数 `0x0001A364` + **单精度浮点**采样率（手写解码，不用 libm）；`info` 块存在时数据偏移要跳过它 |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.aiff`=0x6111、
`std.data.au`=0x6112、`std.data.voc`=0x6113、`std.data.ircam`=0x6114；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-audio2` 与 `STD_AUDIO2_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`（aiff 24 条，
au/voc/ircam 各 23 条）。示例 `examples/stdlegacy2.c6` 四节（17 条自检）。

验收：`make stdtest`（277 个模块、0 failure）、`make std-audio2`（17 条自检全过）、
`make check-all`。

### 11.79 标准库第六十七批（irc / gopher / whois / ident）

第六十七批做「小型文本协议」：IRC 消息、Gopher 菜单、WHOIS 字段与 ident 查询应答。
Gopher 与 WHOIS 是游标（`open`/`next`/`count`/`find`/`rewind`），IRC 与 ident 是
「解析成结构 + 构造回字符串」的一对函数。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.net.irc` | IRC 消息 | `prefix`/`trailing` 去掉前导冒号后以 `Str` 视图给出；`params` 最多 15 个；`irc_build` 的参数里含空格直接返回 -1（要用 trailing 段） |
| `std.net.gopher` | Gopher 菜单 | 每行要四个字段（三个制表符），残缺行**跳过**而不是报错；`i` 类型标 `clean_display`（服务端插的假行）；第一版 `display_name()` 还想去掉首字符，结果 `Icon` 变 `con`，改为原样返回 |
| `std.net.whois` | WHOIS 应答 | 只认「键: 值」，注释与续行跳过；`continuation` 标志告诉调用方下一行是续行（不在缓冲区里拼接）；`get()` 直接给 C 字符串 |
| `std.net.ident` | ident | 查询与应答共用端口对解析；`USERID` 后面必须有系统与用户两段，`ERROR` 只要求一段说明（第一版强制三段，`ERROR : NO-USER` 直接失败） |

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.net.irc`=0x6115、
`std.net.gopher`=0x6116、`std.net.whois`=0x6117、`std.net.ident`=0x6118；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-proto3` 与 `STD_PROTO3_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`（各 24 条用例）。示例
`examples/stdnet2.c6` 四节（22 条自检）。

验收：`make stdtest`（281 个模块、0 failure）、`make std-proto3`（22 条自检全过）、
`make check-all`。

### 11.80 标准库第六十八批（rst / org / tex / pod）

第六十八批做「轻量标记语言」：reStructuredText、Org-mode、LaTeX 与 Perl POD。四个都是
「逐行扫描 + 游标」（`open`/`next`/`count`/`rewind`），只抓结构不解释内容。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.text.rst` | reST | 标题靠「文字 + 下划线」两行，层级按**下划线字符首次出现的顺序**编号；第一版有两处 `keep = *pos` 取在 `rst_line` **之后**，回退时把下一行也吃掉了（`- a/- b/- c` 只能读到 a 和 c），改成先记 `mark = *pos` 再读；段落续行的边界同样用 `mark` |
| `std.text.org` | Org-mode | 标题要拆三样：`*` 层级、`TODO`/`DONE` 关键字、尾部 `:a:b:` 标签（标签从末尾往前扫到空格）；`#+KEY: value` 单独一类；`org_link` 找 `[[url][desc]]` 并支持无 desc 形式 |
| `std.text.tex` | LaTeX | 七级章节命令（允许 `*`），`{...}` 参数按**花括号嵌套**配对；`%` 到行尾是注释（`\%` 不算）；`tex_escape`/`tex_unescape` 处理十个特殊字符，非特殊 `\x` 原样保留 |
| `std.text.pod` | POD | 只解析第一个 POD 块（首个 `=` 命令到 `=cut`）；`pod_strip` 遇到 `X<...>` 要跳过 `X<` 并从内容开头逐字节拷到匹配的 `>`，第一版只 `i++` 导致输出里留下 `<bold>` 的尖括号；`E<lt>`/`E<gt>`/`E<amp>` 等解成字符 |

这一批还踩了**跨模块同名自由函数**的坑：`rst` 与 `pod` 都导出 `kind_name`，同时 import
时链接器只认先到的一个，POD 的条目打印成了 reST 的种类名。改名为 `rst_kind_name` /
`pod_kind_name` 后正常——自由函数一律带模块前缀。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.text.rst`=0x6119、
`std.text.org`=0x611a、`std.text.tex`=0x611b、`std.text.pod`=0x611c；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-markup` 与 `STD_MARKUP_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`（各 24 条用例）。示例
`examples/stdmarkup.c6` 四节（18 条自检）。

验收：`make stdtest`（285 个模块、0 failure）、`make std-markup`（18 条自检全过）、
`make check-all`。

### 11.81 标准库第六十九批（words / sentences / ngram / readability）

第六十九批做「文本分析」：切词、切句、字符 n-gram 与可读性评分。前三个是游标
（`open`/`next`/`count`/`rewind`），可读性用带方法的小统计结构 `ReadStats`。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.text.words` | 切词 | 字母数字下划线算词内字符，`'`/`-` 默认分隔；`keep_joiner(1)` 把连接符并入词并去掉**词尾**连接符；`open(text, fold)` 的第二个参数只记标志（视图不复制，折叠比较交给 `word_eq`） |
| `std.text.sentences` | 切句 | `.`/`!`/`?` 后必须跟空白或结尾才断句，连续标点并在句尾；换行也断句（标题/列表行常常不带句号）；**不处理缩写**，文档里明说 |
| `std.text.ngram` | n-gram | 池由调用方给（`cap × n` 字节），`add` 含重叠、满了就停并返回实际新增数；`freq` 有 `fold` 参数；`unique` 是 O(k²) 比较，池小的时候够用 |
| `std.text.readability` | 可读性 | 音节用元音组启发式（哑 `e` 不算、`-le` 结尾特判、至少 1）；Flesch 与 Flesch-Kincaid 都用 double，`grade()` 四舍五入并夹到 0..20（堆长词的句子算出来是负数，夹到 0） |

这一批的坑还是「字段与同名方法」：`WordDoc.keep_joiner` 与 `keep_joiner()` 撞车，
字段改名 `joiner`。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.text.words`=0x611d、
`std.text.sentences`=0x611e、`std.text.ngram`=0x611f、`std.text.readability`=0x6120；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-textan` 与 `STD_TEXTAN_SRCS`，
`test-all`/`check-all` 都带上它；`words`/`sentences`/`ngram` 在 `stdtest` 里只依赖
`std/text/str.c6`（各 24 条），`readability` 依赖 str + words + sentences（28 条）。
示例 `examples/stdtextan.c6` 四节（22 条自检）。

验收：`make stdtest`（289 个模块、0 failure）、`make std-textan`（22 条自检全过）、
`make check-all`。

### 11.82 标准库第七十批（dotenv / hosts / resolvconf / knownhosts）

第七十批做「系统配置文件」：`.env`、`/etc/hosts`、`/etc/resolv.conf` 与 SSH
`known_hosts`。四个都是游标（`open`/`next`/`count`/`rewind` + 各自的查找方法）。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.dotenv` | `.env` | 引号要**成对**才去掉（`'keep $x'` 单引号内不转义）；无引号值只去掉 ` #` 起的注释（行内独立 `#` 才算，`a#b` 保留）；键名只允许字母数字下划线点；重复键返回第一条 |
| `std.data.hosts` | `/etc/hosts` | `names` 给「第一个主机名到行尾」的整段（含别名），`name` 只给第一个；只有地址没有主机名的行忽略；`lookup` 在整段别名里按词匹配（大小写不敏感） |
| `std.data.resolvconf` | `resolv.conf` | 按首个词分类，未知关键字归 `other` 保留原词；`#` 与 `;` 都是注释；`first_nameserver` 在 `open` 时记住偏移，避免再扫一遍 |
| `std.data.knownhosts` | `known_hosts` | `@cert-authority`/`@revoked` 标记拆到 `marker`；`|1|salt|hash` 哈希条目置 `hashed` 且 `match` 跳过（离线无法验证）；主机列表按逗号切分做大小写不敏感匹配 |

这一批没有新的语法坑，主要是**字节长度**（注释前的别名段、`search` 的域列表等）
需要按实际内容数清楚。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.data.dotenv`=0x6121、
`std.data.hosts`=0x6122、`std.data.resolvconf`=0x6123、`std.data.knownhosts`=0x6124；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-cfg2` 与 `STD_CFG2_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条用例）。示例 `examples/stdcfg.c6` 四节（20 条自检）。

验收：`make stdtest`（293 个模块、0 failure）、`make std-cfg2`（20 条自检全过）、
`make check-all`。

### 11.83 标准库第七十一批（aho / rabin / interval / align）

第七十一批做「匹配与对齐」：Aho-Corasick 多模式自动机、Rabin-Karp 滚动哈希、
区间集合与 Needleman-Wunsch 全局比对。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.text.aho` | 多模式匹配 | 节点存「第一个孩子 + 兄弟链 + 失败指针 + 模式 id/长度」，内存 O(模式总长)；失败指针用队列按层算（孩子下标一定大于父，顺序扫即可）；`scan` 沿失败链收集同一结束位置的多个命中，所以 `aaaa` 里 `aa`×3 + `aaa`×2 都能报出来 |
| `std.text.rabin` | 滚动哈希 | 64 位无符号溢出即 mod 2^64，省掉取模；`pow` 在 `init` 时预算；命中后仍逐字节 `memcmp` 确认，避免哈希碰撞误报 |
| `std.container.interval` | 区间集合 | 闭区间（长度 +1）；`add` 把**相邻**（`hi+1 == lo`）也合并；`remove` 在中间挖洞时先插新段再改旧段尾，容量不足返回 -1 |
| `std.text.align` | 全局比对 | DP 表与输出缓冲都由调用方给；回溯写进临时数组再翻转（避免反向复制）；平分时按「对角→上→左」保证结果稳定；`AlignResult` 的列统计满足 `matches+mismatches+gaps == len` |

这一批的坑：`Aho` 的节点长度一开始想靠失败链推，写乱了两版（还留下无效代码），最后
干脆在节点里加 `len` 字段，命中时 `start = i - len + 1` 一步到位。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.text.aho`=0x6125、
`std.text.rabin`=0x6126、`std.container.interval`=0x6127、`std.text.align`=0x6128；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-match` 与 `STD_MATCH_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条用例）。示例 `examples/stdmatch.c6` 四节（24 条自检）。

验收：`make stdtest`（297 个模块、0 failure）、`make std-match`（24 条自检全过）、
`make check-all`。

### 11.84 标准库第七十二批（future / timer / task / reactor）

第七十二批做「异步编程基础」：Future/Promise、定时器堆、协作式任务与 poll 事件循环。
四个模块都**不读时钟、不开线程**——时间是调用方给的整数毫秒，事件由调用方驱动，
所以全部可以写确定性测试。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.future` | Future | 三态机，第一次 `resolve`/`reject` 生效，之后再调返回 0；`then` 只挂一个回调（完成时立刻调用）；`all`/`any` 是**轮询式**（调用时才看状态），`all` 用「接收者当数组首元素」的写法：`fs[0].all(n, &out)` |
| `std.core.timer` | 定时器 | 小顶堆按 `(deadline, id)` 排序保证触发顺序稳定；`cancel` 先线性找再「下沉+上浮」修复堆；`fire` 里回调可以再 `add`（下一轮触发） |
| `std.core.task` | 协作式调度 | 任务就是 `int step(void*)`，返回 1 继续 / 0 完成；`poll` 被改名 `step_all`，否则与系统 `poll(2)` 的符号冲突（编译期 `redefinition of 'std__core__task__poll'`） |
| `std.os.reactor` | 事件循环 | 复用 `std.os.poll` 的 `PollSet` 与 `std.core.timer`；`run_once(now, max_wait)` 用「最近定时器 − now」与 `max_wait` 的较小值做 poll 超时，既不会空转也不会睡过头；`stop()` 清空注册表，`resume()` 恢复 |

踩到的两个坑：一是 `task` 里的 `poll()` 与 `<poll.h>` 的 `poll` 撞名；二是测试辅助
结构（`Counter`/`CbCtx`/`HitCtx`）定义在文件作用域会进入公共命名空间，和示例里的
同名结构撞车，统一改成 `TaskCounterTest`/`FutureCbTest`/`TimerHitTest`。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.future`=0x6129、
`std.core.timer`=0x612a、`std.core.task`=0x612b、`std.os.reactor`=0x612c；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-async` 与 `STD_ASYNC_SRCS`，`test-all`/`check-all` 都
带上它；`future`/`timer`/`task` 在 `stdtest` 里只依赖 `std/text/str.c6`（各 24 条），
`reactor` 依赖 str + poll + timer（31 条）。示例 `examples/stdasync.c6` 四节（25 条自检）。

验收：`make stdtest`（301 个模块、0 failure）、`make std-async`（25 条自检全过）、
`make check-all`。

### 11.85 标准库第七十三批（executor / asio / retry / awaitgroup）

第七十三批「基于 async 再往上盖一层」：把上批的 Future/定时器/任务/事件循环组合成
可用的异步设施——执行器、按行异步读、退避重试、完成计数组。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.executor` | 主循环 | 内部就是 `Reactor` + `Sched` 的组合；`tick(now, wait)` 顺序是「到期定时器 → fd 事件 → 任务各走一步」，返回 `I/O 事件数 + 本轮步进的任务数`（一开始拿 `step_all()` 的返回值当步进数，结果语义错了：它返回的是**剩余**任务数，改成先取 `active()`） |
| `std.os.asio` | 按行读 | 缓冲由调用方给；`pump()` 读一次、把完整行交回调；**没有回调时不切行**，把整段留在缓冲里给 `next_line`（否则 `next_line` 永远取不到东西）；缓冲满还没换行报 `-2` |
| `std.core.retry` | 退避 | `base × 2^(n-1)` 用循环算（不用 pow，避免浮点）；`max_attempts` 的语义是「最多失败这么多次」，最后一次失败直接 `gave_up` |
| `std.core.awaitgroup` | 完成计数 | 复用 `std.core.future`：归零即结算（值 = 累计 add 总数），失败保留**第一个**错误码；`reset()` 保留绑定，`bind()` 时若已归零则立刻结算 |

这一批的坑：`Executor.now` 字段与 `now()` 方法撞名（`field not found` 之后是
`function pointer expected`），字段改名 `cur`；测试辅助结构再次污染公共命名空间
（`AgCb` 与示例同名），统一加 `…Test` 后缀；`e.after(0, -1, cb, &ctx)` 这种按 C
函数原型写的调用在方法形式下要多一个参数（方法首参隐含），改成 `e.after(-1, cb, &ctx)`。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.executor`=0x612d、
`std.os.asio`=0x612e、`std.core.retry`=0x612f、`std.core.awaitgroup`=0x6130；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-async2` 与 `STD_ASYNC2_SRCS`，`test-all`/`check-all` 都
带上它；四模块在 `stdtest` 里的依赖分别是 executor（str+poll+timer+task+reactor，
35 条）、asio（str+poll+timer+reactor，33 条）、retry（str，24 条）、awaitgroup
（str+future，26 条）。示例 `examples/stdasync2.c6` 四节（32 条自检）。

验收：`make stdtest`（305 个模块、0 failure）、`make std-async2`（32 条自检全过）、
`make check-all`。

### 11.86 标准库第七十四批（solve / ode / optimize / least）

第七十四批做「数值方法与拟合」：求根、定步长积分、一维极值与最小二乘。全部是
`double`，四个模块都要 `-lm`。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.solve` | 求根 | 三种算法统一返回「1 收敛 / 0 迭代用完 / -1 参数错」，结果走 `root()`/`residual()`/`iters()`；牛顿法用中心差分数值导数（`h = 1e-7·max(1,|x|)`），不需要调用方给导数 |
| `std.core.ode` | 积分 | 状态 + 工作缓冲都由调用方给；RK4 的**中间状态必须单独一块缓冲**——第一版把 `k2` 同时当「中间状态」和「导数」就地复用，耦合方程（简谐振子）里 `dy[0] = y[1]; dy[1] = -y[0]` 会读到被覆盖的 `y[0]`，绕一圈回不到初值；改成 `5n` 缓冲（1 个中间状态 + 4 个导数）后正常 |
| `std.core.optimize` | 一维极小 | 黄金分割/三分法/网格细化；测试函数不要加常数偏移——`(x-c)²+1` 在极小点附近两点的函数值会因相对精度变成「相等」，区间就停在 1e-8 量级（这是测试写法的问题，不是算法的问题） |
| `std.core.least` | 拟合 | 正规方程 + 部分主元高斯消元（奇异判据 `1e-12`）；系数最多 8 个；`rss`/`r2` 在拟合时一次算好；`y` 为常数序列时 `r2` 记 1 |

清理：`poly()` 里写过一段只为「占位」的空循环，已删掉；`eval` 这个通用名改成
`value_at`，免得和别的模块撞名。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.solve`=0x6131、
`std.core.ode`=0x6132、`std.core.optimize`=0x6133、`std.core.least`=0x6134；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-math3` 与 `STD_MATH3_SRCS`，`test-all`/`check-all` 都带上
它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`（各 24 条，另需 `-lm`）。示例
`examples/stdmath3.c6` 四节（25 条自检）。

验收：`make stdtest`（309 个模块、0 failure）、`make std-math3`（25 条自检全过）、
`make check-all`。

### 11.87 标准库第七十五批（lu / eigen / tridiag / sparse）

第七十五批做「线性代数」：LU 分解、对称矩阵特征分解、三对角求解与 CSR 稀疏矩阵。
`matrix.c6` 已经提供了稠密矩阵的加减乘、行列式与一次求解，这批补的是**反复使用**
（一次分解多次求解/求逆）与**结构化矩阵**（三对角、稀疏）。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.lu` | LU | PLU 就地分解（下三角是 L、上三角是 U，对角线是 U 的）；`init` 返回秩：满秩给 n、奇异给 0、参数错给 -1；`inverse` 要 `3n` 的 work（e/y/x 三块，一开始只留了 `2n`，`e` 被 `solve` 的 work 覆盖） |
| `std.core.eigen` | 特征分解 | Jacobi 每步旋转最大的非对角元素（比循环扫掠收敛快），`sorted()` 让特征值升序、特征向量跟着换列；幂法的 Rayleigh 商用**新的**归一化向量算 |
| `std.core.tridiag` | 三对角 | Thomas 前代只算一次（`cc[i]`），`solve` 只做右端的消元与回代；`buf` 是 `5n`（lo/di/up 副本 + cc/dd）；主元 < 1e-300 判退化 |
| `std.core.sparse` | CSR | 三块缓冲由调用方给；构建期用 `begin`/`add`/`finish` 游标，**行号不能倒退**（倒退返回 -1，顺带保证 `ptr` 单调）；`transpose` 先用 `ptr2` 数每行个数、再前缀和、再用游标表填值（游标表上限 128 行） |

踩到的坑：`Sparse` 的字段名一开始叫 `rows`/`cols`/`nnz`，正好和方法同名，编译先报
「function pointer expected」再报「method 'rows' must be called」，统统改成
`nr`/`nc`/`nz`；测试里 `fabs` 忘了 `#include <math.h>`，隐式声明按 `int` 返回，
浮点比较全变成了垃圾值；`sparse` 的转置测试期望也算错过一次（`Aᵀ·x` 应该是
`[2,15,4]`）。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.lu`=0x6135、
`std.core.eigen`=0x6136、`std.core.tridiag`=0x6137、`std.core.sparse`=0x6138；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-linalg` 与 `STD_LINALG_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条，另需 `-lm`）。示例 `examples/stdlinalg.c6` 四节（31 条自检）。

验收：`make stdtest`（313 个模块、0 failure）、`make std-linalg`（31 条自检全过）、
`make check-all`。

### 11.88 标准库第七十六批（inverted / bm25 / keyword / stem）

第七十六批做「检索与关键词」：倒排索引、BM25 打分、词频 Top-N 与轻量词干化。
四个模块都只依赖 `std.text.str`（bm25 另需 `-lm`）。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.text.inverted` | 倒排索引 | 词槽（词 slice + 表头）+ 倒排项（doc/tf/next）都用调用方数组；**头插**所以查询结果是「后加入的文档在前」；`add_doc` 里同一个词重复出现就累加 `tf`，不新增倒排项；`and_query` 挑 `df` 最小的词驱动、其余词用 `tf` 过滤，比集合归并简单且够用 |
| `std.text.bm25` | 打分 | 只用「篇数 / 平均长度 / df」三样统计，`add_doc(doclen)` 边走边累加、`finish()` 算平均；`df` 夹到 `[0, N]`、`avgdl` 为 0 时按 1 处理避免除零；测试覆盖了三条单调性（tf↑、dl↑、df↑） |
| `std.text.keyword` | 词频关键词 | 内置 40 多个英文停用词；`add_text` 按非字母数字切分；`top` 用插入排序按「次数降序 + 首次出现序号升序」，所以同频词顺序稳定（`fast` 排在 `far` 前） |
| `std.text.stem` | 词干化 | Porter 的简化版，规则少、可预测；每条后缀都要求「剩余部分含元音」，避免 `fly`/`try` 被削空；`-ee` 结尾（`agree`）不吃词尾 `-e`——第一版把 `agreed` 削成了 `agre`，测试立刻抓到 |

踩到的坑：`InvIndex.terms` 字段和 `terms()` 方法同名（`function pointer expected`），
改成 `slots`；`Keyword.minlen` 字段和 `minlen()` 方法同名，改成 `minl`；`bm25`/`keyword`
的测试里 `fabs` 又要 `-lm`/`<math.h>`。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.text.inverted`=0x6139、
`std.text.bm25`=0x613a、`std.text.keyword`=0x613b、`std.text.stem`=0x613c；`grammar.md`
§8.1 同步。`Makefile` 增加 `std-search2` 与 `STD_SEARCH2_SRCS`，`test-all`/`check-all`
都带上它；四个模块在 `stdtest` 里各 24 条用例。示例 `examples/stdsearch2.c6` 四节
（25 条自检）。

验收：`make stdtest`（317 个模块、0 failure）、`make std-search2`（25 条自检全过）、
`make check-all`。

### 11.89 标准库第七十七批（mst / flow / scc / match）

第七十七批做「图算法进阶」：最小生成树、最大流、强连通分量与二分图匹配。
`graph.c6` 已有 BFS/DFS/拓扑排序/dijkstra，这批补的是另外四大经典算法。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.container.mst` | 最小生成树 | Kruskal 用**稳定**插入排序（同权值按输入顺序）+ 按秩合并的并查集，选中的边就压在排序缓冲前面；Prim 用邻接矩阵（`0` = 无边，只支持正权），从顶点 0 出发，记录边权时用 `key[u]`、父顶点回查最小邻边 |
| `std.container.flow` | 最大流 | 残余容量矩阵 + BFS 最短增广（Edmonds-Karp），同时保留原始容量矩阵以便 `flow_on` = 原容量 − 残余（可为负，表示净流量反向）；`max_flow` 累加到 `value()`，可重复调用继续增广 |
| `std.container.scc` | 强连通分量 | CSR 输入，Tarjan 一次 DFS；分量大小统计复用 `low` 数组（求完就改当计数器）；测试特意断言「分量编号按弹出顺序（汇在前）」 |
| `std.container.match` | 二分图匹配 | Kuhn 的 DFS 增广，`seen` 按**右顶点**记（一开始按左顶点开数组，遍历越界写坏内存，测试直接失败）；`pair_l`/`pair_r` 双向对称 |

踩到的坑：`match` 的 work 分配写成 `2*nleft + nright`，但 `seen` 是按右顶点索引的，
应该 `nleft + 2*nright`，示例与测试里的缓冲也得跟着放大；`mst` 里 `count`/`total`/
`vertices`/`components` 都是通用名（本批内与其他模块无同签名冲突）。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.container.mst`=0x613d、
`std.container.flow`=0x613e、`std.container.scc`=0x613f、`std.container.match`=0x6140；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-graph2` 与 `STD_GRAPH2_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条）。示例 `examples/stdgraph2.c6` 四节（18 条自检）。

验收：`make stdtest`（321 个模块、0 failure）、`make std-graph2`（18 条自检全过）、
`make check-all`。

### 11.90 标准库第七十八批（co / await / scope / limiter：async-await 框架）

第七十八批做「co + async/await」：把前几批的 Future/定时器/执行器组合成一套能**顺序书写
的异步框架**（`std/async/` 新目录）。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.async.co` | co 运行器 | 协程体就是 `int body(void *self, CoAwait *aw)`：返回 1 = 「我要等 `aw`」，返回 0 = 结束；三种挂起点（立即值/未来/定时器）统一走 `CoAwait` 描述；恢复通过执行器的**零延迟定时器**排队而不是递归调用，所以长链条不爆栈 |
| `std.async.await` | async/await 宏 | protothread 写法：`AW_SLEEP(s, aw, ms)` 展开成「`s->pc = __LINE__`；返回挂起描述；`case __LINE__:`」，业务代码保持线性；本编译器**不接受** `do { … case X: } while (0)` 里的 case 标签，改成普通花括号块才行 |
| `std.async.scope` | 结构化并发 | 子协程槽由调用方给（一次性，不复用），`on_done` 回调里减计数并结算 join；`fail(code)` = 记录第一个错误 + 取消其余子协程；`join` 在**还没 spawn** 时不结算（否则会在全空状态就 `resolve(0)`），spawn 里「立刻完成」的分支也复用同一段结算逻辑 |
| `std.async.limiter` | 并发额度 | 只记账不调度：`acquire` 满则排队、`release` 有排队就把额度**转交**队头（`used` 不变、返回它的 token），没有排队才真正归还；计数与队列都是调用方缓冲 |

踩到的坑：`Co` 的字段名一开始叫 `join`/`on_done`，与同名方法冲突（`function pointer
expected`）；`AW_*` 宏用 `do-while` 包裹时编译器报 `';' expected`，换成花括号块后正常；
零延迟定时器会被 `TimerHeap.fire` 的同一次循环取走，所以 `AW_YIELD` 实际上是「立刻继续」
（一次 `tick` 跑完整条链），测试与文档都按这个语义写。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.async.co`=0x6141、
`std.async.await`=0x6142、`std.async.scope`=0x6143、`std.async.limiter`=0x6144；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-async3` 与 `STD_ASYNC3_SRCS`，
`test-all`/`check-all` 都带上它；`stdtest` 里 co 依赖 str+future+poll+timer+task+reactor+
executor（39 条）、await/scope 依赖同一串再加 co（各 41 条）、limiter 只依赖 str（24 条）。
示例 `examples/stdasync3.c6` 四节（24 条自检）。

验收：`make stdtest`（325 个模块、0 failure）、`make std-async3`（24 条自检全过）、
`make check-all`。

### 11.91 标准库第七十九批（hull / polygon / segment / closest：计算几何）

第七十九批做「计算几何」：凸包、简单多边形、线段运算与最近点对。
`geometry.c6` 只有 `Vec2`/`Vec3` 的向量运算，这批补的是**几何算法**。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.core.hull` | 凸包 | Andrew 单调链：排序副本与凸包输出**必须分开**（上凸壳从右往左扫时 `out[k]` 会写到还没读的位置，第一版复用同一块缓冲，结果全错）；`hull_buf` 要 `2n`，最终结果压在开头 `size()` 个；共线点按 `cross <= 0` 剔除 |
| `std.core.polygon` | 多边形 | 有向面积定方向、射线法判点内（边界单独判并算包含）、面积加权形心（退化时按顶点均值）；`edge(i, …)` 是游标式接口，最后一条边自动接回首点 |
| `std.core.segment` | 线段 | 跨立实验 + 共线特判：返回 0/1/2，共线重叠给重叠段中点；`project` 的参数夹到 `[0,1]`，所以 `distance_to_point` 是到**线段**的距离；退化线段（起点=终点）单独处理 |
| `std.core.closest` | 最近点对 | 分治 + 条带按 y 插入排序，每个点最多看 7 个后继；条带缓冲用 `buf + n + lo`（每个递归帧用自己那一段，父帧在子帧返回后才写，互不干扰）；`brute` 留作对拍 |

踩到的坑：`segment` 的测试里我把 (1,1)-(3,1) 当成「与对角线平行且不相交」，其实它的
起点就在对角线上——测试自己错了，改成 (1,0)-(4,3)；`closest` 的字段 `found` 与
`found()` 方法同名（`function pointer expected`），改名 `ok`。

错误号段继续追加（`tcc/tccgen.c` 的 `c6_err_std_slots[]`）：`std.core.hull`=0x6145、
`std.core.polygon`=0x6146、`std.core.segment`=0x6147、`std.core.closest`=0x6148；
`grammar.md` §8.1 同步。`Makefile` 增加 `std-geo2` 与 `STD_GEO2_SRCS`，
`test-all`/`check-all` 都带上它；四个模块在 `stdtest` 里都只依赖 `std/text/str.c6`
（各 24 条，另需 `-lm`）。示例 `examples/stdgeo2.c6` 四节（23 条自检）。

验收：`make stdtest`（329 个模块、0 failure）、`make std-geo2`（23 条自检全过）、
`make check-all`。

### 11.92 标准库第八十批（aio / achan / art / apump：语言级 async 的库支持）

第八十批把标准库的异步支持**建在 c6 的关键字上**（`async`/`await`/`task`/`co` 与运行时
`__c6_co_*`），不再自己造协程框架：`std.async.co`/`await`/`scope`/`limiter` 那批是
用户态调度器，这批是「用语言原语写库」。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.async.aio` | `await c6_io` 之上的收发原语 | `aio_wait`/`aio_read`/`aio_read_exact`/`aio_write_all`/`aio_read_line`；行读的「空行」与「EOF」返回值都是 0，所以补了 `aio_read_line_x(…, &eof)`，`aio_read_line` 就是它的薄包装（async 里 `await` 另一个 async 再 `return` 合法） |
| `std.async.achan` | async 通道 | pipe 上的「4 字节大端长度 + 载荷」帧；单帧上限 4000（`PIPE_BUF` 内，多个发送方也原子）；帧比接收缓冲大时先把这一帧读掉再报错，避免把流搞乱 |
| `std.async.art` | 运行时驱动 | 把 `__c6_co_wait`/`__c6_co_run`/`__c6_co_destroy`/`__c6_co_state` 包成 `Rt` 的方法并记统计；`state` 在任务跑完后是 -1（挂起时非负） |
| `std.async.apump` | 流式转发 | aio + achan 组合：fd→fd 到 EOF、限量、按行（补 `\n`）、通道→fd；`apump_lines` 用 `aio_read_line_x` 区分空行与 EOF |

最大的坑是**编译器约束**：`async` 函数不能是方法（`'async' functions cannot be methods`），
所以这批的 async 函数第一参数一律 `void *`（调用方传 `&ch`），只有非 async 的辅助函数
才做成方法（`ch.init()`、`rt.wait(t)`）。`void *` 首参不会被当成方法，实测可行。

另外两个测试自己写错的坑：`apump_n` 的测试里往 pipe 的**读端**写数据（`write(a[0])`），
以及 `close(c[1])` 在 `__c6_co_wait` 之前——任务还 park 在那个 fd 上，读到 EBADF。

错误号段继续追加：`std.async.aio`=0x6149、`std.async.achan`=0x614a、
`std.async.art`=0x614b、`std.async.apump`=0x614c（`grammar.md` §8.1 同步）。
`Makefile` 增加 `std-async4` 与 `STD_ASYNC4_SRCS`；四个模块在 `stdtest` 里分别 25/27/27/29
条（依赖 `std/text/str.c6`，其中后三者还依赖 `std.async.aio`/`achan`）。示例
`examples/stdasync4.c6` 18 条自检。

验收：`make stdtest`（333 个模块、0 failure）、`make std-async4`（18 条自检全过）、
`make check-all`。

### 11.93 标准库第八十一批（obj / stl / ply / mtl：三维网格与材质）

第八十一批做「三维资产格式」：Wavefront 的 OBJ/MTL 与两种三角网格 STL、PLY。
四个模块都是**游标式**（载入一次记指针，`next_*` 逐条取，`restart` 复位，另给随机访问），
不分配内存，调用方只需要给一段文本/二进制缓冲。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.data.obj` | OBJ 网格 | 逐行按首词分类（`v`/`vn`/`vt`/`f`），`load` 只统计；面索引支持 `1`/`1/1`/`1/1/1` 三种写法，负数按 `base - v` 解析（`-1` = 最后一个顶点）；`bbox` 会临时挪游标再还原 |
| `std.data.stl` | STL 网格 | 识别二进制靠 `84 + 50n == len`；ASCII 数三角形要数 `normal`，**不能数 `facet`**（`endfacet` 里也含）；ASCII 的包围盒在 `load` 里先扫一遍（用前向声明的 `next`）|
| `std.data.ply` | ASCII PLY | 头里的 `element`/`property` 真解析：记 `x`/`y`/`z` 的列号（可以不在前三列），面按 list 处理（先个数再下标）；数据区起点单独记 `dstart`，否则 `restart` 回不到开头 |
| `std.data.mtl` | MTL 材质 | `next` 先清零再填默认值（`Kd=1,1,1`、`d=1`、`Ni=1`），再逐键覆盖；`find` 就是 `restart` + 循环 `next` 比对名字 |

踩到的坑（都是自己写错的）：`obj` 的负数索引第一版写成 `base + v`（`-3` 变成 6），
正确的是 `base - v`；`stl` 的 union 必须显式 `= { 0 }` 初始化（`variable 'c' must be
initialized`）；`ply` 的 `face(i)` 只重置了 `vidx` 没重置 `vpos`，重扫时把面行当顶点读；
`stl`/`mtl` 里数三角形的 `facet` 与默认值顺序各踩一次。

错误号段继续追加：`std.data.obj`=0x614d、`std.data.stl`=0x614e、`std.data.ply`=0x614f、
`std.data.mtl`=0x6150（`grammar.md` §8.1 同步）。`Makefile` 增加 `std-mesh` 与
`STD_MESH_SRCS`；四个模块在 `stdtest` 里分别 25/24/24/24 条（都只依赖 `std/text/str.c6`，
`stl` 另需 `-lm`）。示例 `examples/stdmesh.c6` 24 条自检。

验收：`make stdtest`（337 个模块、0 failure）、`make std-mesh`（24 条自检全过）、
`make check-all`。

### 11.94 标准库第八十二批（同步/异步 HTTPS + 语言级 async 性能指标）

第八十二批补齐「HTTPS 的同步与异步两条路」，并把**语言级 async 框架**（`async`/`await`/
`co`/`task`/`c6_io`）的开销做成可测量的指标。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.net.tlskdf` | TLS 1.3 密钥调度 | `expand_label` 里手拼 `HkdfLabel`（`uint16(length) \| u8(len("tls13 "+label)) \| label \| u8(ctx_len) \| ctx`）；自测向量的期望值是用 Python `hmac`/`hashlib` 独立算出来的，其中 `early_secret` 与 RFC 8448 的 `33ad0a1c…` 逐字节相同，等于双向对齐了规范 |
| `std.net.tlsrec` | TLS 1.3 记录层 | AEAD 严格按 RFC 8439：Poly1305 一次性密钥取 `chacha20_block(key,nonce,0)[0..32]`、密文用 `counter=1`；明文（payload‖type）先落到输出位置再**原地**异或，省掉一块 16 KiB 缓冲；tag 校验失败时不推进序号 |
| `std.net.https` | 同步 HTTPS | fd + 响应缓冲都由调用方给，模块只管组请求、解头、按 `Content-Length` 读满；`use_tls` 之后请求按 23 号记录发出，响应记录解出来喂同一个解析器 |
| `std.net.httpsa` | 异步 HTTPS | 与同步版同一个解析器，只把读写点换成 `await c6_io(fd, ev)`；async 函数不能是方法，所以句柄统一 `void *`；`co` 批量挂起多路，测试里两路同时挂在 `select()` 上 |
| `std.async.abench` | async 性能指标 | 计时用 `now_ns()`（`CLOCK_MONOTONIC`）；三个入口分别量「建/跑/销毁任务」「一次挂起+恢复」「co 批量调度」；报告用手搓整数格式化，不依赖 printf |

实测（本机 x86-64 + TCC 后端，`make std-https` 最后三行）：

```
async 任务创建+驱动+回收: 3000 ops / 521 us -> 5754115 ops/s (173 ns/op)
async 一次挂起+恢复（await c6_io）: 500 ops / 1293 us -> 386592 ops/s (2586 ns/op)
co 顶层任务批跑（__c6_co_run）: 500 ops / 1048 us -> 477048 ops/s (2096 ns/op)
```

踩到的坑：`shared` 是 c6 的保留字（`@shared` 的所有权修饰），参数名得改；`TlsRec`
的字段 `records` 与 `records()` 方法撞名（`function pointer expected`）；`Https`/`HAsync`
的字段 `secure` 与 `secure()` 同样撞名（改 `tls_on`）；`abench` 的 `fails()` 会与调用方
文件里的同名静态变量在同一个符号空间里冲突（`incompatible types for redefinition of
'std__async__abench__fails'`），改名 `fail_count()`；示例里把「响应缓冲」和「body 输出
缓冲」用成同一块，`memcpy` 区域重叠导致 body 读出来是空的。

错误号段继续追加：`std.net.tlskdf`=0x6151、`std.net.tlsrec`=0x6152、
`std.net.https`=0x6153、`std.net.httpsa`=0x6154、`std.async.abench`=0x6155
（`grammar.md` §8.1 同步）。`Makefile` 增加 `std-https` 与 `STD_HTTPS_SRCS`；五个模块在
`stdtest` 里分别 49/34/36/36/31 条。示例 `examples/stdhttps.c6` 25 条自检 + 三行指标。

验收：`make stdtest`（342 个模块、0 failure）、`make std-https`（25 条自检全过）、
`make check-all`。

### 11.95 标准库第八十三批（异步框架的横切面：预算 / 任务组 / 额度 / 取消）

第八十二批把 HTTPS 的两条路铺好、把指标量出来之后，回头看**框架本身**还缺四个横切面：
合作式超时、批量收尾、并发上限、取消传播。这一批补上，并顺手把框架整体写成一篇
`docs/async-framework.md`（分层图 + 三种启动方式 + 实测开销 + 诚实清单）。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.async.abudget` | 时间预算与让出 | 无栈协程不可抢占，`await` 又只能等 fd，所以「超时」只能是**协程自己在安全点查表**：`b.expired()`；`await ayield()` 是 `await` 一个空子任务（不碰 fd、不等时间），`aspin(&b,n)` 连让 n 次 |
| `std.async.agroup` | 任务组 | 不同 async 函数的任务类型不同，句柄收成 `void *`；`join()` 按加入顺序 `__c6_co_wait`。**语义澄清**：返回的是「`__c6_co_wait` 非 0」的个数——只有 `T!error` 声明并以 `err` 结束的 async 函数才会如此，业务上的 `-1` 返回值不算失败（这条是写测试时踩出来的：`aio_read(-1,…)` 的 `val` 是 -1 但 `wait` 返回 0） |
| `std.async.apool` | 并发额度 | 与 `limiter` 分工：`limiter` 排队 + 自带驱动，`apool` 纯计数、拒绝即返回 0、上层自己决定排队还是放弃；`release()` 在空池上返回 -1，防止重复还额度悄悄把上限放大 |
| `std.async.acancel` | 协作取消 | 标志 + 原因码 + 检查计数；`poll()` 读标志并计数（名字避开调用方常见的 `check()` 助手），`link()` 双向联动把父作用域的取消传给子任务；`astep(&c, n)` 是「n 步可取消工作」的样板：每步一个安全点，取消即返回已走步数 |

命名上又撞了两回：`abench.fails()` 和 `acancel.checks()`/`check()` 会和**调用方文件里的
同名静态变量/函数**进同一个符号空间（`incompatible types for redefinition of
'std__async__acancel__check'`），所以方法名一律取「加后缀」的写法
（`fail_count`/`check_count`/`step_count`/`poll`）。

新增一篇 `docs/async-framework.md`：分层（运行时 → 驱动 → 原语 → 组合）、三种启动方式
（`co` / `task` / `spawn`）的生命周期对照、一页速查、实测开销表、以及「这套框架没有做
的事」（时间轮、跨线程唤醒、抢占式取消、一任务一 fd）——避免把限制留给读者猜。

错误号段继续追加：`std.async.abudget`=0x6156、`std.async.agroup`=0x6157、
`std.async.apool`=0x6158、`std.async.acancel`=0x6159。`Makefile` 增加 `std-async5` 与
`STD_ASYNC5_SRCS`；四个模块在 `stdtest` 里分别 31/27/27/24 条。示例
`examples/stdasync5.c6` 34 条自检 + 三行指标。

验收：`make stdtest`（346 个模块、0 failure）、`make std-async5`（34 条自检全过）、
`make check-all`。

### 11.96 语言层：协程个数与 fd 号都不再有固定上限（`select(2)` → `epoll(7)`）

上一版文档里还留着两句老话：「同时存活的协程最多 64 个」「`c6_io` 的 fd 必须在
`[0, 1024)`」。第一句在实现上早就不成立了（任务改成一次 `malloc` 之后没有固定池），
第二句是真的：`tcc_co_pump()` 与 `__c6_co_wait()` 都用 `select(2)` 等就绪，位图是
`unsigned long r[16]`，所以 fd ≥ `FD_SETSIZE`(1024) 的任务会被当成「挂不上去」直接
失败回收。这一批把这一层换成 `epoll(7)`（没有 epoll 的平台退回 `poll(2)`），
两个上限一起消失。

| 位置 | 之前 | 现在 |
| --- | --- | --- |
| `tcc/lib/libtcc1.c` | `select(fd+1, &r, &w, &e)` + `r[16]` 位图；`fd < 1024` 才挂 | 每线程一个 `epoll` 实例：去重登记 `EPOLL_CTL_ADD`/`MOD`、一次 `epoll_wait`、再 `DEL`；注册集与事件数组和 `fds`/`evs`/`leafs` 一起按需 `realloc`；只要 `fd >= 0` 就挂 |
| `tcc/c99-rt_.h` | 同一份代码的 C99 镜像（`fd_set` + `FD_SETSIZE`） | 同一份代码的 epoll 版本（`#include <sys/epoll.h>`，非 Linux 退回 `poll.h`） |
| `std/async/aio.c6` | `aio_wait` 里自己挡了 `fd >= 1024` | 只挡负 fd |
| 文档 | 「最多 64 个」「`[0, 1024)`」 | 「只受内存/`RLIMIT_NOFILE` 约束」 |

`epoll_create1`/`epoll_ctl`/`epoll_wait`（以及退路 `poll`）在 freestanding 链接里仍然是
**弱引用**（和原来的 `select` 一样）：没有 libc 时 pump 报失败而不是链接失败。
`EPOLLERR|EPOLLHUP`（以及 poll 的 `POLLERR|POLLHUP|POLLNVAL`）折成 `rev |= 3`，让任务
看到 EOF/错误而不是永远 park；等待失败时和以前一样返回 -1。

语言层回归（`tests/async.c6`，两条新用例）：

```
PASS: 同时存活 300 个协程（没有固定池上限）
PASS: fd 号超过 FD_SETSIZE（1024）也能挂起
```

第一条同时挂起 300 个 `co` 任务（全 park 在同一个管道上）再 `__c6_co_run()` 跑完；
第二条先 `setrlimit(RLIMIT_NOFILE, 4096)` 再把管道读端 `dup2` 到 **2000** 号，分别用
`co`（pump 路径）和 `task`（`__c6_co_wait` 路径）验证——`select(2)` 时代这两条都会
直接失败。一个 fd 被多个协程 park 时，内核只允许登记一次，所以登记集按 fd 去重、
合并 events，`epoll_wait` 回来后再按 fd 把所有 park 在它上面的叶子一起放行
（上面那条 300 协程共用一个管道的用例正好覆盖这条路径）。

验收：`make check-all`（含 `tests/async.c6` 23 条）。

**后续（同一批的收尾）**：注册集从「每轮 ADD + 每轮 DEL」改成按 fd 索引的数组
+ `EPOLLONESHOT`：登记时 ADD，触发后内核自动解除，下一次 `await` 用
`EPOLL_CTL_MOD` 重装填；等待失败的 fd（被关掉、号码被复用）退化成 ADD。唤醒
一侧改成「fd → 这轮挂在它上面的第一个叶子下标」的 O(1) 命中（同一个 fd 上有
多个叶子时置 `dup` 标记，退回全扫）。1000 路连接一轮收完从 10.7 µs/连接降到
4.95 µs/连接，单次挂起+恢复从 3.95 µs 降到 2.84 µs（`make bench-async`）。

### 11.97 六语言异步编程横向基准（`bench/async/` + `docs/async-comparison.md`）

「异步编程到底贵在哪」不能只靠嘴说，于是把同一件事——等一个 fd 可读，然后
接着跑——在六种语言里各写一遍，同一台机器、单线程口径量四项指标：

| | create (ns/任务) | switch (ns/往返) | conns (µs/连接) | RSS (B/挂起任务) |
|---|---:|---:|---:|---:|
| c6 `async`/`await` + epoll | 146 | 2836 | 5.65 | 369 |
| C `epoll` + 手写状态机 | 13 | 1948 | 2.44 | 90 |
| C++20 协程 + epoll | 22 | 2092 | 1.64 | 168 |
| Rust `async` + 手写执行器 | 45 | 2726 | 3.13 | 197 |
| Uya `@async_fn`/`@await`（探针 + 忙轮询） | 421 | 2545 | — | — |
| Go goroutine + netpoller | 879 | 542 | 3.30 | 2585 |
| C pthread / Rust `std::thread` | 63 568 / 49 901 | 9164 / 8606 | 10.5 / 18.7 | 8565 / 11 735 |

落地物：

| 文件 | 内容 |
|---|---|
| `bench/async/bench.c6` | c6 版（`async`/`await` + `c6_io`） |
| `bench/async/bench_c.c` | C 版：`epoll` 状态机（默认）与 `pthread`（`threads`） |
| `bench/async/bench_cpp.cpp` | C++20 协程 + 200 行 `epoll` 执行器 |
| `bench/async/bench_go.go` | goroutine + `os.Pipe`（走 netpoller） |
| `bench/async/bench_rs.rs` | Rust `async`/`await` + 手写 `epoll + Waker` 执行器 |
| `bench/async/bench.uya` | Uya `@async_fn`/`@await` + `async_wait_readable`（create/switch） |
| `bench/async/run.sh` | 编译 + 分段跑（每指标一个干净进程）+ 打表 |
| `docs/async-comparison.md` | 机制对照、写法对照、数据、口径陷阱、选型建议 |
| `Makefile` | `make bench-async` |

结论写进文档：无栈协程把并发密度提高一个数量级（c6 369 B / Rust 197 B 一路，
Go 2.6 KB，线程 8.5 KB 以上）；`create` 的差距在「谁来分配」（C 状态机一个
指针 vs c6 任务块 + 帧两次 `malloc`）；`switch` 的差距在「几次系统调用」
（走 epoll 的都落在 2～2.9 µs，Go 的 channel 往返 0.55 µs，线程 8～9 µs）；
`conns` 的差距在「每次 await 要不要重新装填」。Uya 只测到 create/switch 两栏
（`block_on` 是探针 + 忙轮询，单任务 `Waker` 最多挂 4 个 fd，单线程多路等待要
走 `scheduler_run_task_queue_*_with_event_loop`，队列版基准留待后续）。c6 的定位是**C 的成本结构、
Go 的写法**：同一个 1000 路循环，c6 版 40 行，C 状态机版 260 行。

### 11.98 标准库第八十四批（静态分析三件套：lex / parse / ub）

前八十三批都是「向外」的库（编码、协议、格式、异步）；这一批向内：**用 c6 写一个
分析 c6 的工具**。三段流水线，分三个模块，`examples/stdub.c6` 是命令行。

| 模块 | 内容 | 关键取舍 |
| --- | --- | --- |
| `std.analysis.lex` | 流式分词器 | `Lexer` 是**纯值**（`src`/`pos`/`line`/`col`/`nerr`），整个结构体赋值就是快照，解析器回滚写 `*self = save` 即可，不需要「保存/恢复 token 流」的 API；token 全是原源码的切片，零分配。关键字单独编号（`kw`）而不是逐次 `str_eq` |
| `std.analysis.parse` | 务实递归下降 | **目标不是完整性，是不中断**：认不出的构造记 `nerr` 并 `ps_resync` 到下一个 `;`/`}`，前 32 处 `(line,col)` 留在 `errl/errc`，让上层能说「这几处没检查」。AST 全放调用方 `Arena`，`Program` 里是函数表/符号表/`nfunc`/`nstmt` |
| `std.analysis.ub` | 13 类 UB 检查 | 每条 `Finding` 带 `Fix{shape,line,text,code}`：`code` 是**可直接粘回源码的整行**（具体变量名/下标/常量，不留占位符）。说不出具体改法的问题**根本不生成**——「可能有风险」对用户没价值。可信度固定两档：常量能折出来/状态能沿直线追出来 = 一定，其余 = 可能 |

**「准确修复建议」是这一批的验收线**，不是附带品：`a[4]` 越界给 `a[3]` 并说明
「非要这个下标就把数组声明成 `int a[5];`」；`free` 后再用给
`defer free(p);   // 放到函数开头…`；`memcpy` 自重叠给 `memmove`；读未初始化堆给
`p = calloc(8, 1);`。

写解析器时踩到的 c6 硬约束（都进了实现，回头看也值得记）：

1. **每个变量定义都要初始化**，工厂函数只能返回「整个已初始化的结构体」——
   于是有 `static Parser ps_open(Arena *, Str)`，`parse_program` 里
   `Parser p = ps_open(a, src); Parser *self = &p;`。
2. **切片字段是 `const`**，只能整片赋值：`s->text = (char[:]){ p, n }`，
   不能 `s->text.ptr = p`。
3. **试探性解析必须有「安静模式」**：`ps_is_typename` 靠「往后猜一眼」判断
   `(T)x` 还是 `(expr)`，猜错不能污染错误账，于是 `Parser.quiet` 计数，
   `ps_err` 在 `quiet > 0` 时直接返回。
4. **`sizeof(*self)` 这类不是类型**：`sizeof` 分支先试类型、失败就当普通表达式，
   否则 `std/container/chan.c6` 一行报 4 个错。

**覆盖率**：把 `std/` 下全部 `.c6` 和绝大多数 `examples/*.c6` 喂给解析器，
`nerr` 全为 0（只有两个 freestanding 例子里带冒号的 `__asm__(… : … : …)` 还
认不出来——走「没认出来」这条路，报告里点名，不猜）。为此补的语法：async 函数的
修饰符、`T[:]`/`T!error` 后缀、结构体成员默认值、`new T`、元组返回/参数/声明、
`shared`、`f"…"`、`Buf@(96)` 泛型实例、`interface` 块、声明语句里的 `catch`、
`foreach ((T a, U b) = x)`。

错误号段继续追加：`std.analysis.lex`=0x615a、`std.analysis.parse`=0x615b、
`std.analysis.ub`=0x615c。`Makefile` 增加 `std-ub` 与 `STD_UB_SRCS`；三个模块在
`stdtest` 里分别 28/43/60 条（各自连带依赖模块的 test 块）。`examples/stdub.c6` 的演示源码覆盖 9 条发现
（一定 8、可能 1）。

验收：`make build`、`make stdtest`（349 个模块、0 failure）、`make std-ub`
（9 条发现、退出码 1）、`make errcheck`（15 通过）、`make selftest`。

### 11.99 静态检查加固：别名 / 跨函数 / 范围 / 守卫 / 安全门

第八十四批把检查器立起来之后，**误报和漏报两端都还不够用**：一遍 `std/` 扫出 164 条
「一定」，其中绝大多数是误报；同时 `free` 走别名、走别的函数、循环变量当下标这些
真问题一条也报不出来。这一轮把两端一起收：

| 能力 | 之前 | 现在 |
| --- | --- | --- |
| 别名 | `q = p; free(p); q[0]` 不报 | 顺拷贝链找根对象，报 use-after-free 并点明「和 p 指向同一块」 |
| 跨函数 | 只能看函数体内 | 每个函数算「无条件释放了第几个参数」的摘要，调用点按摘要改状态；同一个指针传两次会报重复释放 |
| 循环范围 | 下标必须是常量 | `for (i = lo; i < hi; i++)` 记区间，`hi` 越过数组长度就报，消息里给出「i 能取到 0..6」 |
| 守卫 | `if (!p) return;` 之后仍认为 p 可能是 0 | `if (p)` / `p && …` / `!p \|\| …` / `if (!p) return;` 都让 p 变成非空；分支汇合时两边状态不一致就退回「未知」 |
| 出参 | `f(&x)` 之后仍用旧状态 | 传地址就作废该变量状态（内容可能被写） |
| 字符串 | 不检查 | `strcpy/strcat` 的字面量长度对目标数组判「装不下」 |
| 指针算术 | 不检查 | `*(a + k)` 当成 `a[k]` 一起判越界 |
| 位宽 | 一律按 32 位 | 按类型名认 `u64/size_t/i64…`；说不准就当「不知道」，不硬报 |
| 新检查 | — | `UB-BADFREE`：`free(&x)` 释放的不是堆指针（共 14 类） |

精度上的关键一步是**「说不准就不说」**：宽度、别名、范围任何一环拿不到常量，
就退回「可能」或者干脆不报。改完之后拿 `std/` 全部 349 个模块跑一遍：**0 条误报**
（之前是 164 条），而注入用例的真阳性一条没丢（常量类 14/14、别名/跨函数/字面量
字符串/指针算术类 4/8，剩下 4 个是运行期才知道的参数下标——静态上不可判定）。

同时把「没报」变成「查过了」：`examples/stdub.c6` 增 `-W/--strict` 安全门（「可能」
级和「没看懂」的构造都算失败），`Makefile` 增 `make ubcheck` —— 用严格模式把整个
`std/` 过一遍，全绿。文档 `docs/ubcheck.md` 增两节：§4「能追到多远」（每条推理规则
一张表）、§8「通过检查 = 什么级别的安全」（L0–L5 梯子、与 Rust 级的差距、三层叠加
的用法：`stdub -W` + `c6 -b` + 测试）。

随后补上两个能判定的口子：`q = realloc(p, n)` 之后**旧指针作废**（`UB-UAF`；原地
`p = realloc(p, n)` 不算），以及**全局变量地址交给线程入口**（`UB-RACE`，可能级，修法
指向 `std.os.sync` 的锁 / `std.core.atomic` 的原子量）。共 15 类。

运行时那一层也落成了目标：`make safegate = ubcheck + c6 -b`。第二半跑
`tests/ub/ubsmoke.c6`——那处越界下标来自函数参数，静态按「说不准就不说」放过，`-b`
必须当场抓出 `BCHECK: … is outside of the region` 并让进程非 0 退出；抓不住就说明
运行时层没生效，门不放行。

再往前一步是**健全性**：`stdub -S/--sound` 把每处「说不准」的访问都报成
`UB-UNAUDITED` 并算失败（要么补边界判断让检查器能证明，要么标 `/// c6ub-proven`
承认人工审计）。实测整个 `std/`：**365 个模块只有 9 个能过**（deque / span / vec /
align / err / geometry / spline / types / vector），其余 356 个共 **15 380 处**访问
没有证明——这就是「静态证明 vs 裸指针」的事实边界，也是为什么 L5（Rust 级）必须由
语言的所有权/借用类型来提供，而不是检查器的开关。运行时层也量了一遍：`c6 -b` 能抓
堆/栈越界、释放后使用、重复释放、`memcpy` 超长（`exit 255`），抓不到未初始化读、
泄漏、数据竞争。

再往后是**真跨函数**：之前只有「这个函数无条件释放了第几个参数」的一层摘要，两层传递
就断。这一轮改成**调用点内联**——把被调函数体放到当前上下文里再走一遍，实参的堆状态、
堆块字节数、常量值全部带进去，回来把「释放了哪个实参」「写过哪个 `*out`」写回调用方。
为了不组合爆炸，展开有三条硬限制：深度 3、单次函数分析最多 200 次、只展开 ≤48 条语句
的小函数，递归立刻停；展开不了退回一层摘要。效果：`drop2(p) → drop1(p)` 两层传递、
`take(p, 9)` 在调用方知情下的越界、`release(&p)` 里的 `free(*slot)` 全都能追到。
`stdub` 同时支持**目录递归**（给目录就往下钻，只收 `*.c6`，不跟随符号链接，最多 8192 个
文件），`c6 --ub DIR` 一样能用；`make ubcheck` 于是简化成 `./build/stdub -W std`——369 个
模块扫一遍约 6 秒。

顺手补了谓词/返回值契约（`if (!ok(self)) return;`、`r = peek(self,t); if (r == 1)` 之后
self 非空）和「写过 `*out` 就让调用方状态作废」，把误报压回 0（`std/` + `examples/` +
`tests/` 全扫一遍 0 条）。`std/analysis/ub.c6` 用例 82 条。

验收：`make build`、`make ubcheck`（365 个模块全通过、0 条误报、0 处没看懂）、
`make safegate`（静态 + 运行时两层都过）、`make stdtest`（361 个模块、0 failure，
`std/analysis/ub.c6` 71 条用例）、`make std-ub`。

### 11.99 标准库第八十五批（密钥交换 + 传输安全策略 + 抓取许可 + 45 进制）

四个模块都按「结构体方法」写入口，状态型的三家各带一个游标/句柄：

| 模块 | 内容 | test 块 | 方法入口 |
|---|---|---:|---|
| `std/net/x25519.c6` | Curve25519 密钥交换（RFC 7748），16×16 位肢域运算、常数时间蒙哥马利阶梯、条件交换用掩码 | 2 | `Dh.init/public_key/private_key/is_ready/agree` |
| `std/net/hsts.c6` | HSTS 策略库（RFC 6797）：头解析、主机规范化、子域、IP 字面量、过期与撤销（`max-age=0`） | 3 | `HstsStore.init/add/match/remove/find/expired/prune/count` + `HstsDoc.open/next/count/reset` |
| `std/net/robots.c6` | robots.txt（RFC 9309）：逐行扫描、最长匹配、`*`/`$` 通配、具体 agent 组优先、Sitemap 收集 | 2 | `Robots.init/load/allowed/sitemap/count/dropped` + `RobotsDoc.open/next/count/reset` |
| `std/encoding/base45.c6` | Base45（RFC 9285）：一次性编解码 + 流式编码器；空格是字符表成员，只有 `\t\n\r\v\f` 跳过 | 5 | `B45Enc.init/put/put_byte/finish/length/ok` |

集成示例 `examples/stdsec.c6`（`make std-sec`）75 条检查，五节：X25519 握手 →
HSTS 策略 → robots 许可 → base45 指纹 → 四者串联的流水线。`Makefile` 增加
`std-sec` 与 `STD_SEC_SRCS`（`.PHONY`、help、`test-all`、`check-all` 同步）；
err 号段补 `std.net.x25519`=0x615d、`std.encoding.base45`=0x615e、
`std.net.hsts`=0x615f、`std.net.robots`=0x6160（`grammar.md` §8.1 同步）。

`std/` 现在 353 个 `.c6` 文件。各模块的 test 块逐个跑过（2/5/3/2 条，0 failure），
`make std-sec` 75 checks / 0 failures；整体扫一遍由 `make stdtest` / `make check-all`
覆盖。

### 11.100 标准库第八十六批（服务端中间件语义：限流 / 缓存 / 邮件编码 / 模糊匹配）

四个模块都是「判断型」库：不碰 IO、不读时钟，时间由调用方传入（测试里可以喂假时钟）。

| 模块 | 内容 | test 块 | 方法入口 |
|---|---|---:|---|
| `std/net/ratelimit.c6` | 令牌桶（允许突发、整数毫秒补充）+ 滑动窗口（10 个 100 ms 槽，不会在窗口边界双倍突发） | 4 | `Bucket.init/refill/take/take_n/retry_after/avail/capacity/rate/denied`；`Window.init/allow/count/limit/wgranted/wdenied/reset` |
| `std/net/httpcache.c6` | RFC 9111 判断要点：`Cache-Control` 解析、新鲜期（s-maxage 优先）、可存性、ETag 弱/强比较、定容缓存槽（近似 LFU 淘汰）+ 游标 | 3 | `CacheControl.lifetime/storable`；`Cache.init/put/lookup/etag_of/purge/expire/count/evictions`；`CacheDoc.open/next/count/reset` |
| `std/encoding/quotedprintable.c6` | RFC 2045 QP：行宽 76 + 软换行、行尾空白必须编码；只有一份编码状态机（一次性编码与长度函数都复用它） | 3 | `QpEnc.init/put/finish/length/ok`；`qp_encode/qp_decode/qp_decoded_len/qp_valid` |
| `std/text/strdist.c6` | Levenshtein / OSA（相邻换位）/ Hamming / LCS / Jaro / Jaro-Winkler，全部千分制整数；`Matcher` 做「批量模糊找一个」 | 3 | `Matcher.init/consider/best_index/best_score/threshold/count/reset`；`lev/osa/hamming/lcs/jaro_milli/jaro_winkler_milli/similarity_milli` |

集成示例 `examples/stdmw.c6`（`make std-mw`）60 条检查，五节：令牌桶与滑动窗口、
缓存判断与游标、QP 一次性/流式一致、距离与最佳匹配、以及「限流 → 缓存 → 回源」
的请求路径（8 个请求里 4 个被限流、1 个回源、3 个命中缓存）。`Makefile` 增加
`STD_MW_SRCS` / `std-mw`（`.PHONY`、help、`test-all`、`check-all` 同步）；
err 号段补 `std.net.ratelimit`=0x6161、`std.net.httpcache`=0x6162、
`std.encoding.quotedprintable`=0x6163、`std.text.strdist`=0x6164。

`std/` 现在 357 个 `.c6` 文件。各模块 test 块：ratelimit 4、httpcache 3、
quotedprintable 3、strdist 3，逐个跑过 0 failure；`make std-mw` 60 checks / 0 failures。

### 11.101 标准库第八十七批（发现与配置协议 + 文本折叠）

三个协议模块的接口形状刻意统一：`m.parse(buf, len)` 解析、`d.open(&m, buf, len)`
开游标、`d.next(&o)` 逐条取、`b.init/start/option/finish` 构造。

| 模块 | 内容 | test 块 | 方法入口 |
|---|---|---:|---|
| `std/net/coap.c6` | RFC 7252 报文层：4 字节头 + token（≤8）+ 选项串（delta/length 的 13/14 扩展）+ `0xFF` payload；构造器自动算 delta 并校验递增 | 2 | `CoapMsg.parse/code_str`；`CoapDoc.open/next/count/reset`；`CoapBuilder.init/start/token/option/payload/finish` |
| `std/net/dhcp.c6` | RFC 2131/2132 报文层：236 字节头 + magic cookie + TLV 选项（pad/end）+ 300 字节填充；`find` 按编号查而不动游标 | 2 | `DhcpMsg.parse`；`DhcpDoc.open/next/find/count/reset`；`DhcpBuilder.init/header/option/finish`；`ip4_str/ip4_parse` |
| `std/net/ssdp.c6` | UPnP 发现：`M-SEARCH`/`NOTIFY`/应答三种起始行、头部名大小写不敏感、构造器自动写多播 `HOST` | 2 | `SsdpMsg.parse/is_search/is_notify/is_response`；`SsdpDoc.open/next/get/count/reset`；`SsdpBuilder.init/start/header/finish` |
| `std/text/translit.c6` | UTF-8 → 近似 ASCII：拉丁去音符（含 `ß→ss`、`Æ→AE`）、常见符号换 ASCII、全角转半角、组合符号丢弃；`tr_len` 先量后写 | 2 | `Translit.init/put/finish/length/ok/dropped`；`translit/translit_len/decode_cp/map_cp` |

**踩到并写进文档的坑**：`parse` 最初写成「首参是缓冲区」的自由函数，coap 和 dhcp
各一个 `parse` → 链接期被静默合并，调用处只给一句 `incompatible pointer type`
警告，运行结果全错。改成首参结构体指针（`CoapMsg.parse` / `DhcpMsg.parse` /
`SsdpMsg.parse`）后按接收者 tag 编名字，三个模块可以同名共存。这条已写进
`docs/stdlib.md` 的「写法约定」。

集成示例 `examples/stdproto4.c6`（`make std-proto4`）56 条检查。`Makefile` 增加
`STD_PROTO4_SRCS` / `std-proto4`（`.PHONY`、help、`test-all`、`check-all` 同步）；
err 号段补 `std.text.translit`=0x6165、`std.net.coap`=0x6166、`std.net.dhcp`=0x6167、
`std.net.ssdp`=0x6168。`std/` 现在 361 个 `.c6` 文件；四个模块的 test 块逐个跑过
（各 2 条，0 failure），`make std-proto4` 56 checks / 0 failures。

### 11.102 标准库第八十八批（网络运维 + 媒体清单）

| 模块 | 内容 | test 块 | 方法入口 |
|---|---|---:|---|
| `std/net/netflow.c6` | NetFlow v5：24 字节头 + N×48 记录，版本/长度校验，`count` 与实际记录数必须一致 | 2 | `NetflowHeader.parse`；`FlowDoc.open/next/count/reset`；`Flow.src_str/dst_str/next_hop_str/octets_per_packet/duration_ms`；`NetflowBuilder.init/header/record/finish` |
| `std/net/wol.c6` | Wake-on-LAN 魔术包：6 个 0xFF + 16 份 MAC（+ 可选 6 字节 SecureOn 密码），解析要求 16 份完全一致 | 2 | `WolPacket.init/set_mac/set_password/build/parse/data/length/mac_of`；`mac_str/mac_parse` |
| `std/data/vtt.c6` | WebVTT：`WEBVTT` 头、NOTE/STYLE/REGION 跳过、可选 cue id、`-->` 时间戳（毫秒可省小时）、settings、多行正文 | 2 | `VttDoc.open/next/count/reset`；`VttCue.start_str/end_str/duration_ms`；`parse_ts/ts_str` |
| `std/data/zone.c6` | DNS 区域文件：`;` 注释、跨行括号、省略 owner、`$TTL`/`$ORIGIN` 伪记录、`[TTL] [IN] type rdata` | 2 | `ZoneDoc.open/next/count/reset`；`ZoneRec.owner_copy/type_name/rdata_copy`；`type_code_of/type_str` |

集成示例 `examples/stdops.c6`（`make std-ops`）62 条检查：NetFlow/WoL/WebVTT/DNS
区域各一节 + 「找主机 → 唤醒 → 记流 → 出字幕」的串联。`Makefile` 增加
`STD_OPS_SRCS` / `std-ops`（`.PHONY`、help、`test-all`、`check-all` 同步）；
err 号段补 `std.net.netflow`=0x6169、`std.net.wol`=0x616a、`std.data.vtt`=0x616b、
`std.data.zone`=0x616c。`std/` 现在 365 个 `.c6` 文件；四个模块的 test 块逐个跑过
（各 2 条，0 failure）。

### 11.103 标准库第八十九批（文档与邮件）

| 模块 | 内容 | test 块 | 方法入口 |
|---|---|---:|---|
| `std/text/plural.c6` | 英文单复数：不规则表（person/child/mouse…）、不可数表、`-es/-ies/-ves` 规则、计数短语；可自定义增补 | 2 | `Plural.init/of/count_str/count_str_with/add_irregular/add_uncountable` |
| `std/data/mbox.c6` | Unix mailbox：行首 `From ` 切消息、头部区到空行、`>From` 不作分隔、续行折叠 | 2 | `MboxDoc.open/next/count/reset`；`MboxHdrDoc.open_headers/next_header/get_header/header_value/header_count/reset_headers` |
| `std/data/kdl.c6` | KDL v1：节点/参数/属性、嵌套花括号、行注释与块注释、`/-` 软注释、引号转义 | 2 | `KdlDoc.open/next/count/reset`；`KdlArgDoc.open_args/next_arg/arg_count/reset_args` |
| `std/data/rtf.c6` | RTF 纯文本提取：控制字剥离、`\{ \} \\ \'hh` 转义、`\uN`→UTF-8、忽略 `fonttbl`/`info`/`\*` 目标 | 2 | `RtfDoc.open/next_text/plain_text/count/reset` |

**又一条踩坑记录**：`mbox` 的头部游标最初也叫 `next`/`count`/`reset`，与消息游标
同名但参数类型不同 → 编译器按先定义的那个解析，运行时把 `MboxMsg` 的结构写进了
`MboxHdr`，测试直接挂死（无限循环）。改成 `next_header`/`header_count`/`reset_headers`
后正常，原因与结论写进了模块头注释。

集成示例 `examples/stddocs.c6`（`make std-docs`）48 条检查。`Makefile` 增加
`STD_DOCS_SRCS` / `std-docs`（`.PHONY`、help、`test-all`、`check-all` 同步）；
err 号段补 `std.text.plural`=0x616d、`std.data.mbox`=0x616e、`std.data.kdl`=0x616f、
`std.data.rtf`=0x6170。`std/` 现在 369 个 `.c6` 文件；四个模块的 test 块逐个跑过
（各 2 条，0 failure）。

### 11.104 标准库第九十批（网络管理与可观测）

| 模块 | 内容 | test 块 | 方法入口 |
|---|---|---:|---|
| `std/net/prometheus.c6` | 文本暴露格式（语法层）：`# HELP`/`# TYPE`/样本（标签与时间戳按原文本偏移交回），构造器 | 2 | `PromDoc.open/next/count/sample_count/reset`；`PromBuilder.init/help/type/sample/finish` |
| `std/net/gemini.c6` | Gemini 协议报文层：请求行（host/port/path/query）、响应（状态码分类、META、正文），两侧构造器 | 2 | `GeminiReq.parse/host_copy/path_copy`；`GeminiResp.parse/is_*/meta_copy`；`build_request/build_header` |
| `std/net/snmp.c6` | SNMPv2c + 最小 BER：解析 version/community/PDU/request-id/error 字段与 varbind 列表，值按原始字节透传；构造器支持 `set_pdu` 与任意类型 varbind | 2 | `SnmpMsg.parse/community_copy`；`SnmpDoc.open/next/count/reset`；`SnmpVb.oid_str/int_value`；`SnmpBuilder.init/set_pdu/varbind/get/finish` |
| `std/net/nntp.c6` | NNTP 报文层：命令行（512 字节上限、参数禁止空白）、响应状态分类、多行正文边界、去点双写 | 2 | `NntpCmd.init/line/line2/finish`；`NntpResp.parse/is_*/text_copy`；`body_end/undot` |

BSER 这套只做「短格式 + 长格式长度」，不追求通用 ASN.1：SNMP 用到的类型足够，
`std/encoding/asn1` 仍然是通用解析的首选。

集成示例 `examples/stdmgmt.c6`（`make std-mgmt`）53 条检查。`Makefile` 增加
`STD_MGMT_SRCS` / `std-mgmt`（`.PHONY`、help、`test-all`、`check-all` 同步）；
err 号段补 `std.net.prometheus`=0x6171、`std.net.gemini`=0x6172、
`std.net.snmp`=0x6173、`std.net.nntp`=0x6174。`std/` 现在 373 个 `.c6` 文件；
四个模块的 test 块逐个跑过（各 2 条，0 failure）。
