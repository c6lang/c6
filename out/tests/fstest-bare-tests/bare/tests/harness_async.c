/* 自动生成（examples/boot/harness.py）：裸机版 -test harness */
void boot_exit(int code);
int printf(const char *fmt, ...);
int __tcc_err_clear(void);
const char *__tcc_err_msg_get(void);
void __c6_th_join_all(void);

int __tcc_test_0(void);
int __tcc_test_1(void);
int __tcc_test_2(void);
int __tcc_test_3(void);
int __tcc_test_4(void);
int __tcc_test_5(void);
int __tcc_test_6(void);
int __tcc_test_7(void);
int __tcc_test_8(void);
int __tcc_test_9(void);
int __tcc_test_10(void);
int __tcc_test_11(void);
int __tcc_test_12(void);
int __tcc_test_13(void);
int __tcc_test_14(void);
int __tcc_test_15(void);
int __tcc_test_16(void);
int __tcc_test_17(void);
int __tcc_test_18(void);
int __tcc_test_19(void);
int __tcc_test_20(void);
int __tcc_test_21(void);

struct __c6_test_ent {
    int (*fn)(void);
    const char *desc;
};

static struct __c6_test_ent __c6_test_tbl[] = {
    { __tcc_test_0, "await 一个 async 调用拿到返回值" },
    { __tcc_test_1, "await connect(); await send(); a = await recv(); 三种写法" },
    { __tcc_test_2, "挂起不破坏局部变量" },
    { __tcc_test_3, "await 可以嵌套在参数里" },
    { __tcc_test_4, "循环里的 await 反复命中同一个恢复点" },
    { __tcc_test_5, "defer 跨挂起仍然按逆序执行" },
    { __tcc_test_6, "await void 的 async 函数" },
    { __tcc_test_7, "await c6_io 真的挂起再恢复" },
    { __tcc_test_8, "任务槽会被回收，超过池容量也能跑" },
    { __tcc_test_9, "task 声明的句柄能拿到返回值" },
    { __tcc_test_10, "一行多个 task 声明" },
    { __tcc_test_11, "task 句柄在作用域退出时自动回收（超过池容量）" },
    { __tcc_test_12, "co 立刻跑到第一个 await 点" },
    { __tcc_test_13, "多个 co 会被轮转推进（真交替，不是顺序跑完）" },
    { __tcc_test_14, "co 写在 async 体里也是顶层任务" },
    { __tcc_test_15, "co 不产生值，但 __c6_co_run 会报告失败的任务数" },
    { __tcc_test_16, "task 句柄在 err / return 路径上都会被回收（超过池容量）" },
    { __tcc_test_17, "async 的 double!error 失败路径写回 +0.0" },
    { __tcc_test_18, "task 句柄跨挂起保持有效" },
    { __tcc_test_19, "task / co 写在 defer 体里也能工作" },
    { __tcc_test_20, "c6_io 的 fd 不合法时任务被计为失败并回收" },
    { __tcc_test_21, "同时存活 300 个协程（没有固定池上限）" },
};

void _start(void)
{
    int i = 0, nfail = 0;
    int n = (int)(sizeof __c6_test_tbl / sizeof __c6_test_tbl[0]);

    for (i = 0; i < n; i++) {
        int r = 0;
        const char *msg = 0;

        __tcc_err_clear();
        r = __c6_test_tbl[i].fn();
        msg = __tcc_err_msg_get();
        if (r) {
            nfail++;
            printf("FAIL: %s", __c6_test_tbl[i].desc);
            if (msg)
                printf(": %s", msg);
            printf("\n");
        } else {
            printf("PASS: %s\n", __c6_test_tbl[i].desc);
        }
    }
    /* spawn 起的线程必须先收干净，运行时代码才会被释放 */
    __c6_th_join_all();
    printf("%d test(s), %d failure(s)\n", n, nfail);
    boot_exit(nfail ? 1 : 0);
}
