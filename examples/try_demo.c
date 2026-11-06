/* T!error + try + catch 用法演示
 *
 * 编译运行（在仓库根目录）：
 *   make build
 *   ./tcc/tcc -B tcc -std=c6 -run examples/try_demo.c
 */
#include <stdio.h>

/* 错误槽的访问接口（libtcc1 提供） */
int __tcc_err_flag_get(void);
int __tcc_err_id_get(void);
const char *__tcc_err_msg_get(void);
void __tcc_err_clear(void);

/* 1. 用 'T!error' 声明会失败的函数；err(id, msg) 就是 return err() */
static int!error parse(const char *s)
{
    if (!s)
        err(1, "string is NULL");
    if (*s != '1')
        err(2, "expected a leading '1'");
    return s[1] - '0';
}

/* 2. try：成功时原样给出返回值，失败时把错误原样交还给调用者。
 *    'try' 只能用在返回 'T!error' 的函数里，操作数必须是一次调用。 */
static int!error twice(const char *s)
{
    int v = (try parse(s));
    return v * 2;
}

/* 3. catch：拿到 err(id, msg) 的值，并自动清空错误槽；处理块必须自己离开
 *    （return / err() / goto） */
static int handle(const char *s)
{
    int v = twice(s) catch e {
        printf("  caught id=%d msg=%s\n", e.id, e.msg);
        return -1;
    };
    printf("  ok: %d\n", v);
    return v;
}

/* 4. catch 不必绑定变量，也可以写成 catch (e) */
static int handle_plain(const char *s)
{
    twice(s) catch {
        printf("  plain handler, slot flag=%d\n", __tcc_err_flag_get());
        return -2;
    };
    return 0;
}

/* 5. 无返回值的 T!error 函数也可以用 try / catch */
static void!error require_int(const char *s)
{
    if (!s || (*s < '0' || *s > '9'))
        err(3, "not a digit");
}

static int check(const char *s)
{
    require_int(s) catch e {
        printf("  void handler: %s\n", e.msg);
        return -3;
    };
    return 0;
}

int main(void)
{
    printf("1) twice(\"17\") = 14\n");
    handle("17");

    printf("2) twice(NULL) 走 catch\n");
    handle(NULL);

    printf("3) 非数字走 catch\n");
    handle("abc");

    printf("4) 不绑定变量\n");
    handle_plain("x");

    printf("5) void 的 T!error\n");
    printf("   check(\"7\") = %d\n", check("7"));
    printf("   check(\"q\") = %d\n", check("q"));

    printf("6) 错误槽在 catch 之后是干净的：flag=%d\n",
           __tcc_err_flag_get());
    return 0;
}
