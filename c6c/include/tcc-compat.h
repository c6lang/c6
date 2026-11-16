/* tcc-compat.h —— tcc 嵌入 API 的兼容层（安装时按 tcc 的标准头名装出去）。
 *
 * 声明的是 tcc 公开嵌入头里那一整套名字，实现见 src/embed.c6 末尾：能对上的
 * 一一对应到 c6_* API 上，对不上的**如实报错**而不是悄悄什么都不做——所以原本
 * 用 tcc 嵌入库的程序把包含路径和链接库换成 c6c 这套（-lc6c）就能编过，跑得动
 * 的照旧跑，跑不动的在第一次调用处拿到失败返回 + c6_strerror 的说明。
 *
 * 能做的（与 tcc 语义一致）：
 *   tcc_new / tcc_delete / tcc_set_options / tcc_add_include_path /
 *   tcc_add_sysinclude_path / tcc_define_symbol / tcc_undefine_symbol /
 *   tcc_compile_string / tcc_add_file / tcc_relocate / tcc_get_symbol /
 *   tcc_run / tcc_output_file / tcc_list_symbols / tcc_set_error_func。
 *
 * 有语义差别的：
 *   * tcc_set_output_type 只记意图，产物形态由 tcc_run / tcc_output_file 决定；
 *   * tcc_set_lib_path 只用来定位自带运行时（<path>/lib/rt.c6），头文件走
 *     tcc_add_include_path；目录里没有 rt.c6 时失败信息留在 c6_strerror 里；
 *   * tcc_add_sysinclude_path 等价 tcc_add_include_path（c6c 不区分系统头目录）；
 *   * tcc_set_realloc / tcc_set_backtrace_func 是空实现（c6c 的 arena 不可换，
 *     也没有 -b 的运行期回调）。
 *
 * 做不到的（返回 -1 并给出原因）：
 *   tcc_add_library_path / tcc_add_library —— c6c 的链接器没有 -L/-l，只链输入
 *   文件与 libc；tcc_add_symbol —— 宿主地址注入要 JIT 重定位，c6c 走的是
 *   "编到内存镜像再 mmap" 那条路。
 */
#ifndef C6_LIBTCC_COMPAT_H
#define C6_LIBTCC_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct C6State TCCState;

#define TCC_OUTPUT_MEMORY 1
#define TCC_OUTPUT_EXE    2
#define TCC_OUTPUT_DLL    3
#define TCC_OUTPUT_OBJ    4

/* tcc 老版本（0.9.26 及以前）的 tcc_relocate 要传这块指针；现在只认一个参数。
 * 兼容层两个写法都收，见下面的变参声明。 */
#define TCC_RELOCATE_AUTO ((void *)1)

/* 错误回调：编译期诊断改走它（不再直接打到 stderr）。 */
typedef void TCCErrorFunc(void *opaque, const char *msg);

/* 运行期异常回调（tcc 的 -b/-bt）：c6c 没有对应的运行期，收了也只会被忽略。 */
typedef int TCCBtFunc(void *udata, void *pc, const char *file, int line,
                      const char *func, const char *msg);

/* 自定义分配器：c6c 的 arena 不可替换，收了会被忽略。 */
typedef void *TCCReallocFunc(void *ptr, unsigned long size);

TCCState *tcc_new(void);
void tcc_delete(TCCState *s);
void tcc_cleanup(void);
void tcc_set_output_type(TCCState *s, int output_type);
int tcc_add_include_path(TCCState *s, const char *pathname);
int tcc_add_sysinclude_path(TCCState *s, const char *pathname);
void tcc_define_symbol(TCCState *s, const char *sym, const char *value);
void tcc_undefine_symbol(TCCState *s, const char *sym);
int tcc_set_options(TCCState *s, const char *str);
void tcc_set_lib_path(TCCState *s, const char *path);
void tcc_set_error_func(TCCState *s, void *error_opaque, TCCErrorFunc *error_func);
void tcc_set_realloc(TCCReallocFunc *my_realloc);
void tcc_set_backtrace_func(TCCState *s, void *userdata, TCCBtFunc *debug_func);
int tcc_add_library_path(TCCState *s, const char *pathname);
int tcc_add_library(TCCState *s, const char *libraryname);
int tcc_add_symbol(TCCState *s, const char *name, const void *val);
int tcc_compile_string(TCCState *s, const char *buf);
int tcc_add_file(TCCState *s, const char *filename);
/* tcc 现在只传 state；老代码传 (state, TCC_RELOCATE_AUTO)。变参把两种都收下。 */
int tcc_relocate(TCCState *s, ...);
void *tcc_get_symbol(TCCState *s, const char *name);
void tcc_list_symbols(TCCState *s, void *ctx,
                      void (*symbol_cb)(void *ctx, const char *name, const void *val));
int tcc_run(TCCState *s, int argc, char **argv);
int tcc_output_file(TCCState *s, const char *filename);
const char *c6_strerror(TCCState *s);

#ifdef __cplusplus
}
#endif

#endif /* C6_LIBTCC_COMPAT_H */
