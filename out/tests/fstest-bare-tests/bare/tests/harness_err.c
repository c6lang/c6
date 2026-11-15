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

struct __c6_test_ent {
    int (*fn)(void);
    const char *desc;
};

static struct __c6_test_ent __c6_test_tbl[] = {
    { __tcc_test_0, "e1 正常路径只执行 defer，顺序为 41" },
    { __tcc_test_1, "e1 错误路径按逆序执行 errdefer，顺序为 321" },
    { __tcc_test_2, "outer 逐层清理并把内层错误原样传回" },
    { __tcc_test_3, "outer 正常路径只执行 defer，顺序为 AEC" },
    { __tcc_test_4, "loop_err 的 err 展开整个函数帧，顺序为 idd" },
    { __tcc_test_5, "loop_err 正常路径按循环顺序记录 idididz" },
    { __tcc_test_6, "ptr_err 返回哨兵指针并带回错误" },
    { __tcc_test_7, "错误槽可以被覆盖、clear 之后归零" },
    { __tcc_test_8, "err 写入错误码与消息" },
    { __tcc_test_9, "err 之后同一函数里的语句不再执行" },
    { __tcc_test_10, "errdefer 只在错误路径执行" },
    { __tcc_test_11, "清理按注册的逆序执行" },
    { __tcc_test_12, "void 函数里的 err" },
    { __tcc_test_13, "指针返回值也能标注 !error" },
    { __tcc_test_14, "嵌套帧逐层执行清理" },
    { __tcc_test_15, "正常路径不触发清理以外的动作" },
    { __tcc_test_16, "调用方处理错误后可以继续" },
    { __tcc_test_17, "循环中的 err 展开整个函数帧" },
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
