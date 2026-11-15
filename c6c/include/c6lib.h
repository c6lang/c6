/* c6lib.h —— c6c 的嵌入 API：把编译器放进你自己的程序里。
 *
 *     #include <c6lib.h>
 *
 *     C6State *s = c6_new();
 *     c6_add_include_path(s, "include");
 *     if (c6_compile_string(s, "int main(void) { return 42; }") != 0) {
 *         fprintf(stderr, "%s\n", c6_error(s));
 *         return 1;
 *     }
 *     int rc = c6_run(s, argc, argv);      // 就地跑 main，rc == 42
 *     c6_delete(s);
 *
 * 语义和 tcc 的嵌入 API 一一对应（tcc_new / tcc_add_include_path /
 * tcc_define_symbol / tcc_compile_string / tcc_relocate / tcc_get_symbol /
 * tcc_run / tcc_output_file），差别：
 *   * 一个 C6State 只能编一次（编译器的全局状态没做隔离，见 src/embed.c6）；
 *   * 只支持 native 后端（C99 / LLVM 后端产出的是文本）；
 *   * 镜像按固定地址装入（地址被占就报错，不搬迁）。
 *
 * 链接：-lc6c（静态 libc6c.a / 动态 libc6c.so 都行）。
 */
#ifndef C6LIB_H
#define C6LIB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct C6State C6State;

/* 输出类型（目前只影响 c6_output_file 的用法，保留给将来） */
#define C6_OUT_MEM 0
#define C6_OUT_EXE 1
#define C6_OUT_OBJ 2

C6State *c6_new(void);
void c6_delete(C6State *s);
const char *c6_error(C6State *s);

int c6_add_include_path(C6State *s, const char *path);
/* 自带运行时（lib/rt.c6）在哪儿。库形态没有 argv[0] 可推，所以要么让构建
 * 系统在编译库时用 -DC6_DEFAULT_RT=\"...\" 塞一个默认值，要么在这里给，
 * 要么设环境变量 C6C_RT。 */
int c6_set_runtime_path(C6State *s, const char *path);
int c6_define_symbol(C6State *s, const char *name, const char *value);

int c6_compile_string(C6State *s, const char *src);
int c6_compile_file(C6State *s, const char *path);
int c6_relocate(C6State *s);
void *c6_get_symbol(C6State *s, const char *name);
int c6_run(C6State *s, int argc, char **argv);
int c6_output_file(C6State *s, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* C6LIB_H */
