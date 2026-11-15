/* tcc-compat.h —— tcc 嵌入 API 的兼容层（安装时按 tcc 的标准头名装出去）。
 *
 * 声明的是 tcc 那套名字，实现见 src/embed.c6 末尾：tcc_new / tcc_compile_string
 * / tcc_relocate / tcc_run …… 一一对应到 c6_* API 上。所以原本用 tcc 嵌入库的
 * 程序，把包含路径和链接库换成 c6c 这套（-lc6c）就能编过、跑出一样的结果。
 *
 * 支持的范围：内存里编译 + 就地跑（tcc_run）、落盘（tcc_output_file）、
 * 取符号（tcc_get_symbol）、-I/-D/-U 选项（tcc_set_options）。不支持：.o/.a
 * 输入、多 state 并发编译（一个 state 只能编一次）、错误回调（诊断仍走 stderr）。
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

#define TCC_RELOCATE_AUTO ((void *)1)

TCCState *tcc_new(void);
void tcc_delete(TCCState *s);
void tcc_cleanup(void);
void tcc_set_output_type(TCCState *s, int output_type);
int tcc_add_include_path(TCCState *s, const char *pathname);
void tcc_define_symbol(TCCState *s, const char *sym, const char *value);
void tcc_undefine_symbol(TCCState *s, const char *sym);
int tcc_set_options(TCCState *s, const char *str);
int tcc_compile_string(TCCState *s, const char *buf);
int tcc_add_file(TCCState *s, const char *filename);
int tcc_relocate(TCCState *s, void *ptr);
void *tcc_get_symbol(TCCState *s, const char *name);
int tcc_run(TCCState *s, int argc, char **argv);
int tcc_output_file(TCCState *s, const char *filename);
const char *c6_strerror(TCCState *s);

#ifdef __cplusplus
}
#endif

#endif /* C6_LIBTCC_COMPAT_H */
