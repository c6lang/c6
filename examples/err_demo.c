/* err / errdefer / defer 用法演示
 *
 * 编译运行（在仓库根目录）：
 *   make build
 *   ./tcc/tcc -B tcc -std=tcc-ext -run examples/err_demo.c
 *
 * 用 err(id, msg) 的函数必须声明成 'T!error'，调用方必须用 try 或 catch 消费
 * 错误；catch 的处理块必须自己离开（return / err(id, msg) / goto）。
 */
#include <stdio.h>

/* 错误槽的访问接口（libtcc1 提供） */
int __tcc_err_flag_get(void);
int __tcc_err_id_get(void);
const char *__tcc_err_msg_get(void);
void __tcc_err_clear(void);

/* 1. 设置错误：err(id, msg) 记录错误槽、就地展开 errdefer/defer */
static int!error open_file(const char *path)
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

/* 2. void 函数里同样可用：展开清理后直接返回 */
static void!error must_fail(int bad)
{
    defer printf("  void: defer\n");
    if (bad)
        err(7, "void 函数里的错误");
    printf("  void: 正常结束\n");
}

/* 3. errdefer 只在错误路径执行；普通 return 只跑 defer */
static int normal_path(void)
{
    defer printf("  normal: defer\n");
    errdefer printf("  normal: errdefer（不应出现）\n");
    printf("  normal: 正常返回\n");
    return 0;
}

/* 4. 指针返回类型同样可以标注 !error */
static int!error *ptr_fail(int fail)
{
    if (fail)
        err(9, "ptr");
    return 0;
}

/* ---- 演示：catch 的处理块必须自己离开，所以每个场景是一个小函数 ---- */

static int demo_error_path(void)
{
    printf("[1] 错误路径：errdefer 先于 defer，均为逆序\n");
    open_file(NULL) catch e {
        printf("  id=%d msg=%s\n", e.id, e.msg);
        /* catch 已经清空错误槽 */
        printf("  flag after catch=%d\n", __tcc_err_flag_get());
        return 0;
    };
    printf("  unexpected: open_file 没有失败\n");
    return 1;
}

static int demo_handled_by_caller(void)
{
    printf("[2] 调用方处理并继续\n");
    open_file("data.bin") catch e {
        printf("  不应该走到这里: id=%d\n", e.id);
        return 1;
    };
    printf("  调用方继续执行\n");
    return 0;
}

static int demo_void(void)
{
    printf("[3] void 函数\n");
    must_fail(1) catch e {
        printf("  id=%d msg=%s\n", e.id, e.msg);
        return 0;
    };
    printf("  unexpected: must_fail 没有失败\n");
    return 1;
}

/* 'try' 只能出现在返回 T!error 的函数里 */
static int!error demo_propagate(void)
{
    printf("[4] try 原样传播（本帧清理照跑）\n");
    try open_file(NULL);
    printf("  unexpected: open_file 没有失败\n");
    return 1;
}

static int demo_propagate_outer(void)
{
    demo_propagate() catch e {
        printf("  外层捕获: id=%d msg=%s\n", e.id, e.msg);
        return 0;
    };
    return 0;
}

static int demo_normal(void)
{
    printf("[5] 正常路径不执行 errdefer\n");
    normal_path();
    printf("  flag=%d\n", __tcc_err_flag_get());
    return 0;
}

static int demo_ptr(void)
{
    printf("[6] 指针返回值也能标注 !error\n");
    ptr_fail(1) catch e {
        printf("  ptr error id=%d msg=%s\n", e.id, e.msg);
        return 0;
    };
    printf("  unexpected: ptr_fail 没有失败\n");
    return 1;
}

int main(void)
{
    int rc = 0;
    rc |= demo_error_path();
    rc |= demo_handled_by_caller();
    rc |= demo_void();
    rc |= demo_propagate_outer();
    rc |= demo_normal();
    rc |= demo_ptr();
    printf("done, rc=%d\n", rc);
    return rc;
}
