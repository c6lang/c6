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

struct __c6_test_ent {
    int (*fn)(void);
    const char *desc;
};

static struct __c6_test_ent __c6_test_tbl[] = {
    { __tcc_test_0, "closure 类型：大小、typedef、零值" },
    { __tcc_test_1, "推断声明：捕获字面量得到闭包变量" },
    { __tcc_test_2, "标量捕获：快照、可写副本、多次调用保留状态" },
    { __tcc_test_3, "形参捕获与返回闭包" },
    { __tcc_test_4, "结构体捕获：体里改的是 env 副本" },
    { __tcc_test_5, "切片与函数指针捕获" },
    { __tcc_test_6, "托管捕获：闭包持有 @T 的强引用" },
    { __tcc_test_7, "sizeof 捕获：数组只做类型检查" },
    { __tcc_test_8, "遮蔽：体局部遮蔽外层捕获" },
    { __tcc_test_9, "函数指针转闭包：形参、返回值、实参" },
    { __tcc_test_10, "跨翻译单元：同签名闭包按值传递" },
    { __tcc_test_11, "可失败闭包：捕获式与跳板" },
    { __tcc_test_12, "static 局部：只允许 NULL 初始化，运行时再赋值" },
    { __tcc_test_13, "变参捕获字面量：env 才是第一个显式实参" },
    { __tcc_test_14, "struct 返回的闭包：sret 之后才是 env" },
    { __tcc_test_15, "defer 与闭包捕获" },
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
