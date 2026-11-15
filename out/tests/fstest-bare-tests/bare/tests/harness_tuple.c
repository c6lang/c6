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
    { __tcc_test_0, "元组是值类型：布局、元素访问、元素赋值" },
    { __tcc_test_1, "返回多值：解构声明" },
    { __tcc_test_2, "返回多值：解构赋值" },
    { __tcc_test_3, "解构是语句：单条语句体、语句表达式、defer 体里都能用" },
    { __tcc_test_4, "'_' 占位符：只取需要的元素" },
    { __tcc_test_5, "'_' 只在模式里特殊，别处仍是普通标识符" },
    { __tcc_test_6, "元组按值传递、存进 struct、当数组元素" },
    { __tcc_test_7, "混合元素与嵌套元组" },
    { __tcc_test_8, "显式转换与复合字面量" },
    { __tcc_test_9, "#pragma pack 不影响元组的布局" },
    { __tcc_test_10, "元组跨翻译单元共享同一布局" },
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
