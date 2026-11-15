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
    { __tcc_test_0, "stmt 宏交换两个变量" },
    { __tcc_test_1, "宏体里的局部变量自动改名，不捕获同名用户变量" },
    { __tcc_test_2, "__local 仍然可以手动标注" },
    { __tcc_test_3, "expr 宏展开成 ( 表达式 )，实参自动加括号" },
    { __tcc_test_4, "expr 宏可以直接写在声明和语句里" },
    { __tcc_test_5, "stmt 宏的实参可以是语句，并且可以展开多次" },
    { __tcc_test_6, "宏可以调用另一个宏" },
    { __tcc_test_7, "ident 宏给 ## 用，type 宏给声明用" },
    { __tcc_test_8, "角色词在没被宏定义占用时是普通标识符" },
    { __tcc_test_9, "同一个作用域里多次展开不冲突" },
    { __tcc_test_10, "宏的实参里再展开宏（递归组合）" },
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
