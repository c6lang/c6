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

struct __c6_test_ent {
    int (*fn)(void);
    const char *desc;
};

static struct __c6_test_ent __c6_test_tbl[] = {
    { __tcc_test_0, "数组：下标从 0 开始，元素按顺序" },
    { __tcc_test_1, "值绑定是副本，写它不改数组" },
    { __tcc_test_2, "元素是结构体时也是副本" },
    { __tcc_test_3, "切片参数与复合字面量切片" },
    { __tcc_test_4, "字符串字面量是 char 数组，含结尾 NUL" },
    { __tcc_test_5, "区间 0:n 是半开区间" },
    { __tcc_test_6, "区间是空集时一次都不进" },
    { __tcc_test_7, "区间支持负数和 long 界" },
    { __tcc_test_8, "可迭代值只求值一次（切片是快照）" },
    { __tcc_test_9, "'_' 丢弃绑定" },
    { __tcc_test_10, "break、continue、defer 与 for 一致" },
    { __tcc_test_11, "嵌套 foreach，break 只跳出一层" },
    { __tcc_test_12, "绑定名遮蔽外层同名变量，出了循环不泄漏" },
    { __tcc_test_13, "下标是 size_t，写它不改迭代（绑定是副本）" },
    { __tcc_test_14, "foreach 的隐藏槽跨 await 挂起仍然有效" },
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
