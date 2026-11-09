/* c6 字符串插值 'f"..{expr}.."' 示例
 *
 *   ./tcc/tcc -B tcc -std=c6 -run examples/fstr_demo.c
 *
 * f".." 把字面文本和插值拼成一个 const char *（借用本深度的临时缓冲），
 * @f".." 把拼好的文本做成一个托管对象（@char，生命周期跟着引用走）。
 * 没有格式化语法：宽度、精度、进制都要先自己格式化好再插进来。
 */
#include <stdio.h>
#include <string.h>

struct Point { int x, y; };

/* 插值接受任意表达式，各段从左到右求值一次 */
static int bump(int *p)
{
    *p += 1;
    return *p;
}

/* 结果可以当返回值传出去（借用只在函数内有效，要留住就 @f".."） */
static @char saved(int n)
{
    return @f"saved #{n}";          /* 托管：调用者拿到的对象自己计数 */
}

int main(void)
{
    int n = 42;
    int i = 0;
    char c = 'A';
    _Bool ok = 1;
    char[:] text = "hello";
    @char keep = @f"kept {n}";
    struct Point p = { 1, 2 };
    const char *q = NULL;

    /* 整数、字符、布尔（'A' 按 C 的规则是 int，要字符就用 char 变量） */
    printf("1: %s\n", f"n={n} c={c} b={ok} cmp={ n > 0 }");
    /* 文本：C 字符串、字符数组、字符切片、托管缓冲 */
    printf("2: %s\n", f"[{ \"abc\" }] [{ text }] [{ keep }]");
    /* 指针：空指针是 (null)/0x0，其它是地址 */
    printf("3: %s %d\n", f"null={q}", q == NULL);
    printf("4: %s\n", f"&p.x={ &p.x }");
    /* 花括号、转义 */
    printf("5: %s\n", f"{{}} literal, tab:\there \"quoted\"");
    /* 任意表达式，只求值一次 */
    printf("6: %s %d\n", f"{bump(&i)} {bump(&i)}", i);
    /* 嵌套：每深一层多一个反斜杠 */
    printf("7: %s\n", f"a{f\"b{f\\\"c{n}\\\"}b\"}a");
    /* 托管结果可以存进别的托管槽，也可以当普通 const char * 读 */
    {
        @char a = saved(1);
        shared @char s = @f"shared {n}";
        printf("8: %s %s %s len=%llu\n", a, s, keep,
               __c6_str_len(keep));
    }
    /* 循环里每个 @f".." 都释放，全程不涨引用 */
    {
        int k = 0;
        for (k = 0; k < 1000; k++) {
            @char tmp = @f"loop {k}";
            if (tmp[0] != 'l')
                return 1;
        }
        printf("9: 1000 个 @f\"..\" 全部释放，live=%llu\n",
               __c6_gc_live());
    }
    /* 借用：同一个深度只有一块缓冲，下一次 f".." 会覆盖它 */
    {
        const char *first = f"first {n}";
        const char *second = f"second {n}";
        printf("10: %s / %s (借用会被覆盖)\n", second, first);
    }
    /* 普通 C 字符串照旧：f 仍可以是标识符 */
    {
        int f = 7;
        printf("11: f=%d, %s\n", f, "plain C string");
    }
    printf("12: 长度 %d\n", (int)strlen(f"{n}{n}{n}"));
    return 0;
}
