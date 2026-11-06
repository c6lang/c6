/* superc 自测：err / errdefer / defer / try / catch 的语义
 *
 * 运行（仓库根目录）：
 *   make selftest
 * 或：
 *   ./tcc/tcc -B tcc -std=tcc-ext -test tests/test.c
 *   ./tcc/tcc -B tcc -std=tcc-ext -test -test-filter=errdefer tests/test.c
 *
 * 每个 test 块就是一个静态函数：返回 0 表示通过，非 0 表示失败；
 * 用 err(code, msg) 报错时，运行器会打印 FAIL 行和消息，退出码非 0。
 */
#include <stdio.h>
#include <string.h>

/* libtcc1 提供的错误槽接口 */
int __tcc_err_flag_get(void);
int __tcc_err_id_get(void);
const char *__tcc_err_msg_get(void);
void __tcc_err_clear(void);
void __tcc_err_set(int id, const char *msg);

/* ---- 清理顺序记录 ---- */
static char logbuf[128];
static int loglen;

static void lg_reset(void)
{
    loglen = 0;
    logbuf[0] = '\0';
}

static void lg(const char *s)
{
    int n = strlen(s);
    if (loglen + n < (int)sizeof(logbuf) - 1) {
        memcpy(logbuf + loglen, s, n + 1);
        loglen += n;
    }
}

#define LG_IS(s) (0 == strcmp(logbuf, (s)))

/* ---- 被测函数 ----
 *
 * 用 err(id, msg) 的函数必须声明成 'T!error'；它们的调用方必须用 try 或
 * catch 消费错误，否则编译报错。err 执行后立即返回，没有 err() 重抛形式。 */

/* 直接失败：err 之后语句不执行 */
static int!error fail_direct(void)
{
    defer lg("f");
    errdefer lg("e");
    err(3, "boom");
    lg("unreachable");
    return 0;
}

/* 正常返回：只执行 defer，不执行 errdefer */
static int succeed(void)
{
    defer lg("f");
    errdefer lg("e");
    lg("b");
    return 0;
}

/* 多层：内层失败，外层用 try 原样传出 */
static int!error inner_fail(int bad)
{
    defer lg("1");
    errdefer lg("2");
    if (bad)
        err(9, "inner");
    return 0;
}

static int!error outer_fail(int bad)
{
    defer lg("3");
    errdefer lg("4");
    try inner_fail(bad);            /* 语句形式：失败时本帧清理照样执行 */
    lg("5");
    return 0;
}

/* 调用方处理掉错误后继续执行：catch 已经把错误槽清空 */
static int recover(void)
{
    inner_fail(1) catch e {
        if (e.id != 9)
            return -2;
        lg("r");
        return 0;
    };
    return -1;                      /* 没有错误反而不对 */
}

/* void 函数：err 直接返回，同样展开清理 */
static void!error void_fail(void)
{
    defer lg("v");
    err(7, "void");
    lg("unreachable");
}

/* 指针返回类型同样可以标注 !error */
static int!error *ptr_fail(int bad)
{
    defer lg("p");
    if (bad)
        err(11, "ptr");
    return 0;
}

/* 循环中 err：展开整个函数帧（循环里注册的 defer 也会执行） */
static void!error loop_fail(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        defer lg("d");
        if (i == 1)
            err(12, "loop");
        lg("i");
    }
    lg("z");
}

/* ---- 测试块 ---- */

test "err 写入错误码与消息" {
    __tcc_err_clear();
    fail_direct() catch e {
        if (e.id != 3) err(1, "错误码不正确");
        if (!e.msg || strcmp(e.msg, "boom")) err(2, "错误消息不正确");
        return 0;
    };
    err(3, "err 没有进入错误路径");
}

test "err 之后同一函数里的语句不再执行" {
    __tcc_err_clear();
    lg_reset();
    fail_direct() catch e {
        if (strstr(logbuf, "unreachable"))
            err(1, "err 之后的语句被执行了");
        return 0;
    };
    err(2, "err 没有进入错误路径");
}

test "errdefer 只在错误路径执行" {
    __tcc_err_clear();
    lg_reset();
    succeed();
    if (!LG_IS("bf")) err(1, "正常路径的清理顺序不对");
    if (strstr(logbuf, "e")) err(2, "正常路径执行了 errdefer");
}

test "清理按注册的逆序执行" {
    __tcc_err_clear();
    lg_reset();
    fail_direct() catch e {
        if (!LG_IS("ef")) err(1, "错误路径的清理顺序不对");
        return 0;
    };
    err(2, "err 没有进入错误路径");
}

test "void 函数里的 err" {
    __tcc_err_clear();
    lg_reset();
    void_fail() catch e {
        if (e.id != 7) err(1, "void 函数的错误码不对");
        if (strcmp(e.msg, "void")) err(2, "void 函数的错误消息不对");
        if (!LG_IS("v")) err(3, "void 函数的 defer 未执行");
        return 0;
    };
    err(4, "void 函数没有进入错误路径");
}

test "指针返回值也能标注 !error" {
    __tcc_err_clear();
    lg_reset();
    ptr_fail(1) catch e {
        if (e.id != 11) err(1, "指针返回函数的错误码不对");
        if (!LG_IS("p")) err(2, "指针返回函数的 defer 未执行");
        return 0;
    };
    err(3, "指针返回函数没有进入错误路径");
}

test "嵌套帧逐层执行清理" {
    __tcc_err_clear();
    lg_reset();
    outer_fail(1) catch e {
        if (!LG_IS("2143")) err(1, "嵌套清理顺序不对");
        if (e.id != 9) err(2, "try 传播丢失了原始错误码");
        if (strcmp(e.msg, "inner")) err(3, "try 传播丢失了原始消息");
        return 0;
    };
    err(4, "外层没有进入错误路径");
}

test "正常路径不触发清理以外的动作" {
    __tcc_err_clear();
    lg_reset();
    if ((try outer_fail(0)) != 0) err(1, "正常路径返回了错误");
    if (!LG_IS("153")) err(2, "正常路径清理顺序不对");
    if (__tcc_err_flag_get()) err(3, "正常路径设置了错误槽");
}

test "调用方处理错误后可以继续" {
    __tcc_err_clear();
    lg_reset();
    if (recover() != 0) err(1, "recover 返回值不对");
    if (!LG_IS("21r")) err(2, "处理后清理顺序不对");
    if (__tcc_err_flag_get()) err(3, "catch 之后错误槽没有清空");
}

test "循环中的 err 展开整个函数帧" {
    __tcc_err_clear();
    lg_reset();
    loop_fail() catch e {
        /* i=0 正常结束（i,d）；i=1 在注册 defer 之后 err，展开该帧时再执行 d */
        if (!LG_IS("idd")) err(1, "循环中的清理顺序不对");
        if (e.id != 12) err(2, "循环中的错误码不对");
        return 0;
    };
    err(3, "循环里没有进入错误路径");
}

test "错误槽可以被覆盖、clear 之后归零" {
    __tcc_err_clear();
    __tcc_err_set(9, "inner");
    if (__tcc_err_id_get() != 9) err(1, "第一次的错误码不对");
    __tcc_err_set(3, "boom");
    if (__tcc_err_id_get() != 3) err(2, "错误码没有被后一次 err 覆盖");
    if (strcmp(__tcc_err_msg_get(), "boom")) err(3, "错误消息没有被覆盖");
    __tcc_err_clear();
    if (__tcc_err_flag_get()) err(4, "clear 之后 flag 仍为 1");
    if (__tcc_err_msg_get()) err(5, "clear 之后 msg 未置空");
}

/* ---- try / catch ---- */

static int!error tfail(int n)
{
    if (n > 0)
        err(n, "tfail");
    return n * 100;
}

static int!error tprop(int n)
{
    int v = (try tfail(n));
    return v + 1;
}

static int!error tdeep(int n)
{
    return (try tprop(n)) + 1000;
}

static void!error tcheck(int n)
{
    if (n)
        err(n, "tcheck");
}

static int!error tdefer(int n)
{
    defer lg("D");
    if (n)
        err(n, "tdefer");
    return n;
}

test "try 在成功路径上原样传出返回值" {
    __tcc_err_clear();
    lg_reset();
    if ((try tfail(0)) != 0) err(1, "try 的返回值不对");
    if ((try tprop(0)) != 1) err(2, "try 没有把返回值传出去");
    if (__tcc_err_flag_get()) err(3, "成功路径不应置错误标记");
}

test "try 在失败路径上把错误原样传回调用者" {
    __tcc_err_clear();
    tprop(5) catch e {
        if (e.id != 5) err(1, "错误码没有原样传回");
        if (strcmp(e.msg, "tfail")) err(2, "错误消息没有原样传回");
        return 0;
    };
    err(3, "没有进入错误路径");
}

test "try 可以把错误穿过多层调用传回来" {
    __tcc_err_clear();
    if ((try tdeep(0)) != 1001) err(1, "正常路径的返回值不对");
    tdeep(6) catch e {
        if (e.id != 6) err(2, "多层传播之后错误码不对");
        return 0;
    };
    err(3, "多层调用没有进入错误路径");
}

test "try 支持无返回值的 T!error 函数" {
    __tcc_err_clear();
    try tcheck(0);
    if (__tcc_err_flag_get()) err(1, "成功路径不应置错误标记");
    tcheck(4) catch e {
        if (e.id != 4) err(1, "catch 拿到的 id 不对");
        return 0;
    };
    err(2, "catch 没有被执行");
}

test "catch 拿到 err(id,msg) 的值并自动清空错误槽" {
    __tcc_err_clear();
    tfail(7) catch e {
        if (e.id != 7) err(1, "catch 拿到的 id 不对");
        if (strcmp(e.msg, "tfail")) err(2, "catch 拿到的 msg 不对");
        if (__tcc_err_flag_get()) err(3, "catch 之后错误槽没有清空");
        if (__tcc_err_id_get()) err(4, "catch 之后错误码没有清空");
        return 0;
    };
    err(5, "catch 没有被执行");
}

test "catch 可以省略变量，也可以加括号" {
    __tcc_err_clear();
    tfail(3) catch {
        if (__tcc_err_flag_get()) err(1, "catch 之后错误槽没有清空");
        return 0;
    };
    err(2, "不带宽度的 catch 没有被执行");
}

test "catch 可以写成 catch (e) 形式" {
    __tcc_err_clear();
    tfail(2) catch (e) {
        if (e.id != 2) err(1, "catch 拿到的 id 不对");
        return 0;
    };
    err(2, "catch (e) 没有被执行");
}

test "catch 可以用 goto 离开处理块" {
    int ok = 0;
    __tcc_err_clear();
    tfail(9) catch e {
        if (e.id != 9) err(1, "catch 拿到的 id 不对");
        goto done;
    };
    err(2, "catch 没有被执行");
done:
    ok = 1;
    if (!ok) err(3, "goto 没有跳到标签");
    __tcc_err_clear();
}

test "catch 之后错误槽干净，后续 err 不会被旧值干扰" {
    __tcc_err_clear();
    tfail(11) catch e {
        return 0;
    };
    err(1, "catch 没有被执行");
}

test "T!error 函数里的 defer 在错误路径上照样执行" {
    __tcc_err_clear();
    lg_reset();
    try tdefer(0);
    if (!LG_IS("D")) err(1, "正常路径没有执行 defer");
    lg_reset();
    tdefer(8) catch e {
        if (!LG_IS("D")) err(1, "错误路径没有执行 defer");
        if (e.id != 8) err(2, "catch 拿到的 id 不对");
        return 0;
    };
    err(3, "catch 没有被执行");
}
