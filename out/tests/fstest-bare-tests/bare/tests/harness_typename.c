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
    { __tcc_test_0, "声明、成员访问与 sizeof" },
    { __tcc_test_1, "指针、数组与 '->'：语句开头的 'Vec *p' 是声明" },
    { __tcc_test_2, "形参与返回值" },
    { __tcc_test_3, "结构体成员与自引用" },
    { __tcc_test_4, "union 与 enum 的裸名" },
    { __tcc_test_5, "sizeof、强转与复合字面量" },
    { __tcc_test_6, "切片元素用裸 tag" },
    { __tcc_test_7, "方法是普通函数：裸 tag 写接收者" },
    { __tcc_test_8, "限定符与 typedef" },
    { __tcc_test_9, "typeof / _Generic / _Alignof 里的裸 tag" },
    { __tcc_test_10, "接口方法签名里的裸 tag" },
    { __tcc_test_11, "遮蔽：同名函数优先，struct 形式照旧" },
    { __tcc_test_12, "遮蔽：同名局部变量优先（按表达式解析）" },
    { __tcc_test_13, "真实头文件：struct stat 与 stat() 共存" },
    { __tcc_test_14, "标签 'TAG:' 仍然是标签" },
    { __tcc_test_15, "跨翻译单元：两个 TU 都写裸 tag" },
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
