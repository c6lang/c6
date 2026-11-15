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

struct __c6_test_ent {
    int (*fn)(void);
    const char *desc;
};

static struct __c6_test_ent __c6_test_tbl[] = {
    { __tcc_test_0, "值形态：从 n 倒数到 1，原对象保持副本语义" },
    { __tcc_test_1, "数组的元素类型本身是迭代器时，仍按数组迭代" },
    { __tcc_test_2, "指针形态：迭代器状态就在原对象里" },
    { __tcc_test_3, "接口值：vtable 分派，self 指向原对象" },
    { __tcc_test_4, "元素是结构体：绑定是 next() 返回值的副本" },
    { __tcc_test_5, "元素是指针：绑定就是 next() 的返回值" },
    { __tcc_test_6, "'_' 绑定仍然推进迭代器" },
    { __tcc_test_7, "托管元素：值绑定与 '_' 都不泄漏" },
    { __tcc_test_8, "托管迭代器：own 与 borrow 的隐藏槽" },
    { __tcc_test_9, "托管临时迭代器：隐藏槽接管所有权" },
    { __tcc_test_10, "数组的托管元素绑定也不泄漏" },
    { __tcc_test_11, "union 迭代器：值形态与指针形态" },
    { __tcc_test_12, "跨 TU 的 has()/next() 原型" },
    { __tcc_test_13, "可迭代值只求值一次" },
    { __tcc_test_14, "has() 首次为假：零次迭代" },
    { __tcc_test_15, "无限迭代器 + break" },
    { __tcc_test_16, "continue 回到 has() 判断" },
    { __tcc_test_17, "defer 与 for 一致" },
    { __tcc_test_18, "绑定不泄漏到外层" },
    { __tcc_test_19, "嵌套：迭代器里套迭代器" },
    { __tcc_test_20, "隐藏槽跨 await 挂起仍然有效" },
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
