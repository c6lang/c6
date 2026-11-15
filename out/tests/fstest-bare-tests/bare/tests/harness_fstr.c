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
    { __tcc_test_0, "整型、布尔、字符" },
    { __tcc_test_1, "文本：C 字符串、数组、托管缓冲、字符切片" },
    { __tcc_test_2, "指针" },
    { __tcc_test_3, "花括号与转义" },
    { __tcc_test_4, "插值可以是任意表达式" },
    { __tcc_test_5, "嵌套的 f\"..\"" },
    { __tcc_test_6, "空串和长文本" },
    { __tcc_test_7, "@f\"..\"：托管对象的生命周期跟着引用走" },
    { __tcc_test_8, "循环里的 @f\"..\" 每个都释放" },
    { __tcc_test_9, "普通 f\"..\" 借用本深度的临时缓冲" },
    { __tcc_test_10, "err(f\"..\") 的消息会被复制下来" },
    { __tcc_test_11, "宏里的 f\"..\"" },
    { __tcc_test_12, "格式说明 {expr;spec}" },
    { __tcc_test_13, "浮点" },
    { __tcc_test_14, "跨翻译单元" },
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
