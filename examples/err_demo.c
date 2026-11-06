/* err / errdefer / defer 用法演示
 *
 * 编译运行（在仓库根目录）：
 *   make build
 *   ./tcc/tcc -B tcc -std=tcc-ext -run examples/err_demo.c
 */
#include <stdio.h>

/* 错误槽的访问接口（libtcc1 提供） */
int __tcc_err_flag_get(void);
int __tcc_err_id_get(void);
const char *__tcc_err_msg_get(void);
void __tcc_err_clear(void);

/* 1. 设置错误：err(id, msg) 记录错误槽、就地展开 errdefer/defer、返回哨兵 -1 */
static int open_file(const char *path)
{
    defer printf("  defer: 关闭文件\n");
    errdefer printf("  errdefer: 回滚已做的修改\n");

    if (!path) {
        err(1, "path is NULL");     /* 不会执行到下一行 */
        printf("unreachable\n");
    }
    printf("  ok: %s\n", path);
    return 0;
}

/* 2. 调用方检查哨兵：处理掉就 __tcc_err_clear()，否则 err(); 原样重抛 */
static int handled_by_caller(const char *path)
{
    if (open_file(path) == -1) {
        printf("  调用方处理: id=%d msg=%s\n",
               __tcc_err_id_get(), __tcc_err_msg_get());
        __tcc_err_clear();
    }
    return 0;
}

static int propagate(void)
{
    if (open_file(NULL) == -1)
        err();                      /* 不覆盖 id/msg，继续向上传播 */
    return 0;
}

/* 3. void 函数里同样可用：展开清理后直接返回 */
static void must_fail(int bad)
{
    defer printf("  void: defer\n");
    if (bad)
        err(7, "void 函数里的错误");
    printf("  void: 正常结束\n");
}

/* 4. errdefer 只在错误路径执行；普通 return 只跑 defer */
static int normal_path(void)
{
    defer printf("  normal: defer\n");
    errdefer printf("  normal: errdefer（不应出现）\n");
    printf("  normal: 正常返回\n");
    return 0;
}

/* 5. 指针返回类型也拿哨兵 -1 */
static int *ptr_fail(int fail)
{
    if (fail)
        err(9, "ptr");
    return 0;
}

int main(void)
{
    printf("[1] 错误路径：errdefer 先于 defer，均为逆序\n");
    propagate();
    printf("  id=%d msg=%s flag=%d\n", __tcc_err_id_get(),
           __tcc_err_msg_get(), __tcc_err_flag_get());
    __tcc_err_clear();

    printf("[2] 调用方处理\n");
    handled_by_caller(NULL);

    printf("[3] void 函数\n");
    must_fail(1);
    printf("  id=%d msg=%s\n", __tcc_err_id_get(), __tcc_err_msg_get());
    __tcc_err_clear();

    printf("[4] 正常路径不执行 errdefer\n");
    normal_path();
    printf("  flag=%d\n", __tcc_err_flag_get());

    printf("[5] 返回值：err 返回 -1\n");
    printf("  rc=%d\n", open_file(NULL));
    __tcc_err_clear();

    printf("[6] 指针返回值同样是 -1\n");
    printf("  is-sentinel=%d\n", ptr_fail(1) == (int *)-1);
    __tcc_err_clear();
    return 0;
}
