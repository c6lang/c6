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
    { __tcc_test_0, "try 在成功路径上原样传出返回值" },
    { __tcc_test_1, "try 在失败路径上把错误原样传回调用者" },
    { __tcc_test_2, "try 可以把错误穿过多层调用传回来" },
    { __tcc_test_3, "try 支持无返回值的 T!error 函数" },
    { __tcc_test_4, "catch 拿到 err(id,msg) 的值并自动清空错误槽" },
    { __tcc_test_5, "catch 可以省略变量，也可以加括号" },
    { __tcc_test_6, "catch 可以写成 catch (e) 形式" },
    { __tcc_test_7, "catch 可以用 goto 离开处理块" },
    { __tcc_test_8, "catch 之后错误槽干净，后续 err 不会被旧值干扰" },
    { __tcc_test_9, "T!error 函数里的 defer 在错误路径上照样执行" },
    { __tcc_test_10, "try 传出成功路径的返回值并执行后续语句" },
    { __tcc_test_11, "catch 快照错误码与消息，处理块内错误槽已清空" },
    { __tcc_test_12, "catch 可以省略变量、加括号或用 goto 离开" },
    { __tcc_test_13, "void!error 函数也能用 try 和 catch" },
    { __tcc_test_14, "循环里的 try 一旦失败就中止整个函数" },
    { __tcc_test_15, "嵌套 try 传播最先发生的错误" },
    { __tcc_test_16, "声明符后的 !error 标记与原型一致" },
    { __tcc_test_17, "double!error 支持成功与错误路径" },
    { __tcc_test_18, "try 在 double!error 里传播错误" },
    { __tcc_test_19, "float!error 也能用" },
    { __tcc_test_20, "long double!error 也能用" },
    { __tcc_test_21, "错误槽最终保持干净" },
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
