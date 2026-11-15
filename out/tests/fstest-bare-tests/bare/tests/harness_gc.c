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

struct __c6_test_ent {
    int (*fn)(void);
    const char *desc;
};

static struct __c6_test_ent __c6_test_tbl[] = {
    { __tcc_test_0, "new 分配、零初始化、作用域退出自动释放" },
    { __tcc_test_1, "p = NULL 释放；重复 NULL 无害" },
    { __tcc_test_2, "共享引用：p = q 计数加一，自赋值净变化为 0" },
    { __tcc_test_3, "return 把引用交给调用者" },
    { __tcc_test_4, "err() 传播时释放局部对象" },
    { __tcc_test_5, "丢弃的 new 立刻释放" },
    { __tcc_test_6, "new 作实参：调用返回后释放" },
    { __tcc_test_7, "own @T：独占，退出作用域销毁" },
    { __tcc_test_8, "weak @T：对象死了就变空" },
    { __tcc_test_9, "托管接收者的方法调用不加计数、不读旧值" },
    { __tcc_test_10, "循环里声明：每次迭代都释放" },
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
