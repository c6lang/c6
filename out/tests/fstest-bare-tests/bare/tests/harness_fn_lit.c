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
    { __tcc_test_0, "显式声明、就地调用、推断声明" },
    { __tcc_test_1, "返回值、多个参数、无参数" },
    { __tcc_test_2, "体里看得见文件作用域的函数和全局变量" },
    { __tcc_test_3, "体里看得见文件作用域的 typedef 和 tag" },
    { __tcc_test_4, "返回结构体、切片和托管引用" },
    { __tcc_test_5, "变参字面量" },
    { __tcc_test_6, "可失败字面量：err 与 catch" },
    { __tcc_test_7, "体里可以用 defer" },
    { __tcc_test_8, "字面量里再写字面量" },
    { __tcc_test_9, "static inline 体里的字面量" },
    { __tcc_test_10, "作为实参、存进结构体、放进数组" },
    { __tcc_test_11, "赋给已有的函数指针" },
    { __tcc_test_12, "交给 C 库：qsort" },
    { __tcc_test_13, "文件作用域和 static 初始化的字面量" },
    { __tcc_test_14, "每个字面量各有自己的函数" },
    { __tcc_test_15, "推断声明得到的变量就是普通函数指针" },
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
