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
int __tcc_test_22(void);
int __tcc_test_23(void);
int __tcc_test_24(void);
int __tcc_test_25(void);

struct __c6_test_ent {
    int (*fn)(void);
    const char *desc;
};

static struct __c6_test_ent __c6_test_tbl[] = {
    { __tcc_test_0, "单形参实例的字段就是绑定的类型" },
    { __tcc_test_1, "不同实参是不同的类型" },
    { __tcc_test_2, "两个实参要写括号，顺序不能错" },
    { __tcc_test_3, "模板可以自引用" },
    { __tcc_test_4, "嵌套应用：Vect@Vect@int" },
    { __tcc_test_5, "模板里可以再放实例" },
    { __tcc_test_6, "实例就是普通结构体，方法按接收者定类型" },
    { __tcc_test_7, "实例可以作返回值、参数和文件作用域变量" },
    { __tcc_test_8, "重复应用同一个实参共享同一个类型" },
    { __tcc_test_9, "切片的元素可以是实例" },
    { __tcc_test_10, "元组的元素可以是实例" },
    { __tcc_test_11, "实例类型可以在函数体内第一次产生" },
    { __tcc_test_12, "指针和数组类型可以作实参（要括号）" },
    { __tcc_test_13, "空的实例也照常工作" },
    { __tcc_test_14, "值形参当数组长度" },
    { __tcc_test_15, "值实参可以是常量表达式（含负数）" },
    { __tcc_test_16, "只有值形参的模板、位域宽度" },
    { __tcc_test_17, "值形参在方法模板里" },
    { __tcc_test_18, "体内方法：字段和参数直接读写" },
    { __tcc_test_19, "体内方法用值形参、先写的方法调用后写的" },
    { __tcc_test_20, "体内方法的符号和 '|T|' 方法一样" },
    { __tcc_test_21, "体内方法里第一次实例化另一个模板" },
    { __tcc_test_22, "多翻译单元：泛型表按 TU 复位" },
    { __tcc_test_23, "体内方法的形参表：void、空表、const" },
    { __tcc_test_24, "托管实例上的方法不碰引用计数" },
    { __tcc_test_25, "体内方法里的实例再实例" },
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
