/* c6 自测：err() / errdefer / 清理顺序
 *
 * 检查 err 的返回值、清理顺序（LG_IS 比对日志）、错误槽接口，以及
 * catch 拿到的 e.id / e.msg。全部是直接断言，不比较 stdout。
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/err.c
 */
#include <stdio.h>
#include <string.h>

/* libtcc1 提供的错误槽接口 */
int __tcc_err_flag_get(void);
int __tcc_err_id_get(void);
const char *__tcc_err_msg_get(void);
void __tcc_err_clear(void);
void __tcc_err_set(int id, const char *msg);

#define SENT (-1)

/* ---- 清理顺序记录 ---- */
static char logbuf[256] = {0};
static int loglen = 0;

static void lg_reset(void)
{
    loglen = 0;
    logbuf[0] = '\0';
}

static void lg(const char *s)
{
    while (*s && loglen < (int)sizeof(logbuf) - 2)
        logbuf[loglen++] = *s++;
    logbuf[loglen] = '\0';
}

#define LG_IS(s) (0 == strcmp(logbuf, (s)))

/* catch 会清空错误槽，所以先把 catch 拿到的错误值抄出来 */
static int last_id = 0;
static const char *last_msg = 0;

static void note(int id, const char *msg)
{
    last_id = id;
    last_msg = msg;
}

/* ---- 被测函数（来自 157_c6_err.c） ---- */

/* errdefer 正常路径跳过、错误路径逆序执行；defer 两条路径都执行 */
static int!error e1(int fail)
{
    defer lg("1");
    errdefer lg("2");
    errdefer lg("3");
    if (fail)
        err(7, "boom");
    lg("4");
    return 0;
}

static int!error inner(int fail)
{
    defer lg("A");
    errdefer lg("B");
    if (fail)
        err(9, "inner");
    return 0;
}

/* 跨两帧传播：try 在跑完本帧清理之后把错误原样交回 */
static int!error outer(int fail)
{
    defer lg("C");
    errdefer lg("D");
    try inner(fail);
    lg("E");
    return 0;
}

/* 循环里的 err 展开整个函数帧 */
static void!error loop_err(int fail)
{
    int i = 0;
    for (i = 0; i < 3; i++) {
        defer lg("d");
        if (fail && i == 1)
            err(12, "loop");
        lg("i");
    }
    lg("z");
}

static int!error *ptr_err(int fail)
{
    defer lg("p");
    if (fail)
        err(11, "ptr");
    return 0;
}

/* 只要"返回值或哨兵"的调用方：catch 之后报 SENT */
static int run_e1(int fail)
{
    int v = e1(fail) catch e {
        note(e.id, e.msg);
        return SENT;
    };
    return v;
}

static int run_outer(int fail)
{
    int v = outer(fail) catch e {
        note(e.id, e.msg);
        return SENT;
    };
    return v;
}

static int run_loop(int fail)
{
    loop_err(fail) catch e {
        note(e.id, e.msg);
        return SENT;
    };
    return 0;
}

static int *run_ptr(int fail)
{
    int *v = ptr_err(fail) catch e {
        note(e.id, e.msg);
        return (int *)(long)SENT;
    };
    return v;
}

/* ---- 被测函数 ---- */

/* 直接失败：err 之后的语句不执行 */
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
    int i = 0;
    for (i = 0; i < 3; i++) {
        defer lg("d");
        if (i == 1)
            err(12, "loop");
        lg("i");
    }
    lg("z");
}

/* ---- 157_c6_err.expect 的逐行覆盖 ---- */

/* expect: e1-normal=0 e1-normal: 41 / flag=0 */
test "e1 正常路径只执行 defer，顺序为 41" {
    int v = 0;
    __tcc_err_clear();
    lg_reset();
    v = run_e1(0);
    if (v != 0) err(1, "e1 正常路径的返回值不对");
    if (!LG_IS("41")) err(2, "e1 正常路径的清理顺序不对");
    if (__tcc_err_flag_get()) err(3, "e1 正常路径设置了错误槽");
}

/* expect: e1-error=-1 e1-error: 321 / id=7 msg=boom */
test "e1 错误路径按逆序执行 errdefer，顺序为 321" {
    int v = 0;
    __tcc_err_clear();
    lg_reset();
    last_id = 0;
    last_msg = 0;
    v = run_e1(1);
    if (v != SENT) err(1, "e1 错误路径没有返回错误哨兵值");
    if (!LG_IS("321")) err(2, "e1 错误路径的清理顺序不对");
    if (last_id != 7) err(3, "e1 带回的错误码不对");
    if (!last_msg || strcmp(last_msg, "boom")) err(4, "e1 带回的错误消息不对");
}

/* expect: outer-error=-1 outer-error: BADC / id=9 msg=inner */
test "outer 逐层清理并把内层错误原样传回" {
    int v = 0;
    __tcc_err_clear();
    lg_reset();
    last_id = 0;
    last_msg = 0;
    v = run_outer(1);
    if (v != SENT) err(1, "outer 错误路径没有返回错误哨兵值");
    if (!LG_IS("BADC")) err(2, "跨帧传播时的清理顺序不对");
    if (last_id != 9) err(3, "try 传播丢失了原始错误码");
    if (!last_msg || strcmp(last_msg, "inner")) err(4, "try 传播丢失了原始消息");
}

/* expect: outer-normal=0 outer-normal: AEC / flag=0 */
test "outer 正常路径只执行 defer，顺序为 AEC" {
    int v = 0;
    __tcc_err_clear();
    lg_reset();
    v = run_outer(0);
    if (v != 0) err(1, "outer 正常路径的返回值不对");
    if (!LG_IS("AEC")) err(2, "outer 正常路径的清理顺序不对");
    if (__tcc_err_flag_get()) err(3, "outer 正常路径设置了错误槽");
}

/* expect: loop-error: idd / id=12 */
test "loop_err 的 err 展开整个函数帧，顺序为 idd" {
    int v = 0;
    __tcc_err_clear();
    lg_reset();
    last_id = 0;
    v = run_loop(1);
    if (v != SENT) err(1, "loop_err 错误路径没有返回错误哨兵值");
    if (!LG_IS("idd")) err(2, "循环中的清理顺序不对");
    if (last_id != 12) err(3, "循环中的错误码不对");
}

/* expect: loop-normal: idididz */
test "loop_err 正常路径按循环顺序记录 idididz" {
    int v = 0;
    __tcc_err_clear();
    lg_reset();
    v = run_loop(0);
    if (v != 0) err(1, "loop_err 正常路径的返回值不对");
    if (!LG_IS("idididz")) err(2, "循环正常路径的记录顺序不对");
    if (__tcc_err_flag_get()) err(3, "loop_err 正常路径设置了错误槽");
}

/* expect: ptr=1 ptr: p / id=11 msg=ptr */
test "ptr_err 返回哨兵指针并带回错误" {
    int *p = 0;
    __tcc_err_clear();
    lg_reset();
    last_id = 0;
    last_msg = 0;
    p = run_ptr(1);
    if (p != (int *)(long)SENT) err(1, "ptr_err 没有返回哨兵指针");
    if (!LG_IS("p")) err(2, "ptr_err 的 defer 未执行");
    if (last_id != 11) err(3, "ptr_err 带回的错误码不对");
    if (!last_msg || strcmp(last_msg, "ptr")) err(4, "ptr_err 带回的错误消息不对");
}

/* expect: final-flag=0（以及 test.c 的错误槽覆盖/清零检查） */
test "错误槽可以被覆盖、clear 之后归零" {
    __tcc_err_clear();
    __tcc_err_set(9, "inner");
    if (__tcc_err_id_get() != 9) err(1, "第一次的错误码不对");
    __tcc_err_set(3, "boom");
    if (__tcc_err_id_get() != 3) err(2, "错误码没有被后一次 err 覆盖");
    if (!__tcc_err_msg_get() || strcmp(__tcc_err_msg_get(), "boom"))
        err(3, "错误消息没有被覆盖");
    __tcc_err_clear();
    if (__tcc_err_flag_get()) err(4, "clear 之后 flag 仍为 1");
    if (__tcc_err_id_get()) err(5, "clear 之后 id 未归零");
    if (__tcc_err_msg_get()) err(6, "clear 之后 msg 未置空");
}

/* ---- 单点语义断言 ---- */

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
        if (!e.msg || strcmp(e.msg, "void")) err(2, "void 函数的错误消息不对");
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
        if (!e.msg || strcmp(e.msg, "inner")) err(3, "try 传播丢失了原始消息");
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
