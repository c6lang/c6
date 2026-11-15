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
    { __tcc_test_0, "数组隐式转切片，.len/.data 正确" },
    { __tcc_test_1, "切片是视图：写穿到数组，数组改动也反映回来" },
    { __tcc_test_2, "四种区间写法 [lo:hi]、[lo:]、[:hi]、[:]" },
    { __tcc_test_3, "空区间合法，长度为 0" },
    { __tcc_test_4, "切片作为参数和返回值" },
    { __tcc_test_5, "切片作为结构体成员" },
    { __tcc_test_6, "切片数组" },
    { __tcc_test_7, "指向切片的指针" },
    { __tcc_test_8, "结构体元素的切片可以写成员" },
    { __tcc_test_9, "字符串字面量也是切片，长度含结尾 NUL" },
    { __tcc_test_10, "数组元素的切片（typedef 起的数组类型）" },
    { __tcc_test_11, "复合字面量用指针和长度构造切片" },
    { __tcc_test_12, "sizeof：切片两字，元素是普通对象" },
    { __tcc_test_13, "循环写穿、下标副作用" },
    { __tcc_test_14, "切片跨翻译单元共享同一个布局" },
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
