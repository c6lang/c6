/* superc 自测：err / errdefer / defer 的语义
 *
 * 运行（仓库根目录）：
 *   make selftest
 * 或：
 *   ./tcc/tcc -B tcc -std=tcc-ext -test test.c
 *   ./tcc/tcc -B tcc -std=tcc-ext -test -test-filter=errdefer test.c
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

/* ---- 被测函数 ---- */

/* 直接失败：err 之后语句不执行，返回值是 -1 */
static int fail_direct(void)
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

/* 多层：内层失败，外层检查哨兵后原样重抛 */
static int inner_fail(int bad)
{
    defer lg("1");
    errdefer lg("2");
    if (bad)
        err(9, "inner");
    return 0;
}

static int outer_fail(int bad)
{
    defer lg("3");
    errdefer lg("4");
    if (inner_fail(bad) == -1)
        err();                  /* 重抛：保留 id/msg */
    lg("5");
    return 0;
}

/* 调用方处理掉错误后清空错误槽，函数继续正常返回 */
static int recover(void)
{
    if (inner_fail(1) == -1) {
        if (__tcc_err_id_get() == 9)
            __tcc_err_clear();
        lg("r");
    }
    return 0;
}

/* void 函数：err 直接返回，同样展开清理 */
static void void_fail(void)
{
    defer lg("v");
    err(7, "void");
    lg("unreachable");
}

/* 指针返回类型：哨兵同样是 -1 */
static int *ptr_fail(int bad)
{
    defer lg("p");
    if (bad)
        err(11, "ptr");
    return 0;
}

/* 循环中 err：展开整个函数帧（循环里注册的 defer 也会执行） */
static void loop_fail(void)
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

test "err 写入错误码与消息并返回 -1" {
    int rc;
    __tcc_err_clear();
    rc = fail_direct();
    if (rc != -1) err(1, "err 未返回哨兵 -1");
    if (__tcc_err_flag_get() != 1) err(2, "错误槽 flag 未置位");
    if (__tcc_err_id_get() != 3) err(3, "错误码不正确");
    if (!__tcc_err_msg_get() || strcmp(__tcc_err_msg_get(), "boom"))
        err(4, "错误消息不正确");
    __tcc_err_clear();
}

test "err 之后同一函数里的语句不再执行" {
    __tcc_err_clear();
    lg_reset();
    fail_direct();
    if (strstr(logbuf, "unreachable"))
        err(1, "err 之后的语句被执行了");
    __tcc_err_clear();
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
    fail_direct();
    if (!LG_IS("ef")) err(1, "错误路径的清理顺序不对");
    __tcc_err_clear();
}

test "void 函数里的 err" {
    __tcc_err_clear();
    lg_reset();
    void_fail();
    if (__tcc_err_id_get() != 7) err(1, "void 函数的错误码不对");
    if (strcmp(__tcc_err_msg_get(), "void")) err(2, "void 函数的错误消息不对");
    if (!LG_IS("v")) err(3, "void 函数的 defer 未执行");
    __tcc_err_clear();
}

test "指针返回值也是 -1 哨兵" {
    __tcc_err_clear();
    lg_reset();
    if (ptr_fail(1) != (int *)-1) err(1, "指针返回值不是 -1");
    if (!LG_IS("p")) err(2, "指针返回函数的 defer 未执行");
    __tcc_err_clear();
}

test "嵌套帧逐层执行清理" {
    __tcc_err_clear();
    lg_reset();
    if (outer_fail(1) != -1) err(1, "外层没有返回哨兵");
    if (!LG_IS("2143")) err(2, "嵌套清理顺序不对");
    if (__tcc_err_id_get() != 9) err(3, "err() 重抛丢失了原始错误码");
    if (strcmp(__tcc_err_msg_get(), "inner")) err(4, "err() 重抛丢失了原始消息");
    __tcc_err_clear();
}

test "正常路径不触发重抛" {
    __tcc_err_clear();
    lg_reset();
    if (outer_fail(0) != 0) err(1, "正常路径返回了错误");
    if (!LG_IS("153")) err(2, "正常路径清理顺序不对");
    if (__tcc_err_flag_get()) err(3, "正常路径设置了错误槽");
}

test "调用方处理错误后可以继续" {
    __tcc_err_clear();
    lg_reset();
    if (recover() != 0) err(1, "recover 返回值不对");
    if (!LG_IS("21r")) err(2, "处理后清理顺序不对");
    if (__tcc_err_flag_get()) err(3, "错误槽没有清空");
}

test "循环中的 err 展开整个函数帧" {
    __tcc_err_clear();
    lg_reset();
    loop_fail();
    /* i=0 正常结束（i,d）；i=1 在注册 defer 之后 err，展开该帧时再执行 d */
    if (!LG_IS("idd")) err(1, "循环中的清理顺序不对");
    if (__tcc_err_id_get() != 12) err(2, "循环中的错误码不对");
    __tcc_err_clear();
}

test "错误槽被后一次 err 覆盖、clear 之后归零" {
    __tcc_err_clear();
    lg_reset();
    inner_fail(1);
    if (__tcc_err_id_get() != 9) err(1, "第一次的错误码不对");
    fail_direct();                      /* 覆盖成 id=3 "boom" */
    if (__tcc_err_id_get() != 3) err(2, "错误码没有被后一次 err 覆盖");
    if (strcmp(__tcc_err_msg_get(), "boom")) err(3, "错误消息没有被覆盖");
    __tcc_err_clear();
    if (__tcc_err_flag_get()) err(3, "clear 之后 flag 仍为 1");
    if (__tcc_err_msg_get()) err(4, "clear 之后 msg 未置空");
}
