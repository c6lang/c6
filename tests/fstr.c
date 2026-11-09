/* c6 自测：字符串插值 f"..{expr}.."
 *
 * 'f".."' 是 const char *：文本由运行时的构造器拼出来，插值可以是任意
 * 表达式。'@f".."' 是同一个构造器的托管版本，结果是一个 @char 对象。
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/fstr.c tests/tu/fstr.c
 */
#include <stdio.h>
#include <string.h>

/* 另一个翻译单元（tests/tu/fstr.c） */
int tu_text_len(const char *s);
int!error tu_boom(int n, const char *msg);

/* 托管版本只在本 TU 里验证（见下面的用例） */
static @char mk_msg(int n)
{
    return @f"local{n}";
}

static int eq(const char *a, const char *b)
{
    return 0 == strcmp(a, b);
}

/* catch 拿到的错误值：catch 体只能以 return / err() / goto 收尾，
 * 所以先抄到全局里再断言 */
static int last_id = 0;
static const char *last_msg = 0;

static int run_boom(int n)
{
    int v = tu_boom(n, f"bad {n}") catch e {
        last_id = e.id;
        last_msg = e.msg;
        return -1;
    };
    return v;
}

test "整型、布尔、字符" {
    int n = 42;
    unsigned u = 7;
    long long ll = -5;
    _Bool t = 1, f = 0;
    char c = 'Q';

    if (!eq(f"n={n}", "n=42"))
        err(1, "int 插值不对");
    if (!eq(f"{u}/{ll}", "7/-5"))
        err(2, "无符号和 long long 插值不对");
    if (!eq(f"{t}{f}", "truefalse"))
        err(3, "_Bool 应该输出 true / false");
    if (!eq(f"{c}", "Q"))
        err(4, "char 应该输出字符本身");
    if (!eq(f"{'A'}", "65"))
        err(5, "字符常量按 C 的规则是 int，应该输出 65");
    if (!eq(f"{(int)c}", "81"))
        err(5, "char 转 int 后应该输出数值");
}

test "文本：C 字符串、数组、托管缓冲、字符切片" {
    const char *s = "abc";
    char a[4] = "xy";
    char[:] sl = "slice";
    int n = 1;
    @char m = @f"m{n}";

    if (!eq(f"[{s}]", "[abc]"))
        err(1, "const char * 插值不对");
    if (!eq(f"[{a}]", "[xy]"))
        err(2, "char 数组插值不对");
    if (!eq(f"[{sl}]", "[slice]"))
        err(3, "char[:] 插值不该带上结尾的 0");
    if (!eq(f"[{m}]", "[m1]"))
        err(4, "@char 插值不对");
}

test "指针" {
    int x = 5;
    int *p = &x;
    const char *np = 0;

    if (!eq(f"{np}", "(null)"))
        err(1, "空的 const char * 应该输出 (null)");
    if (!eq(f"{(void *)0}", "0x0"))
        err(2, "空指针应该输出 0x0");
    if (0 != strncmp(f"{p}", "0x", 2))
        err(3, "指针应该输出成 0x 开头的地址");
}

test "花括号与转义" {
    int n = 1;

    if (!eq(f"{{}}", "{}"))
        err(1, "'{{}}' 应该输出 '{}'");
    if (!eq(f"{{{n}}}", "{1}"))
        err(2, "转义花括号中间夹插值不对");
    if (!eq(f"a}}b", "a}b"))
        err(3, "'}}' 应该输出 '}'");
    if (!eq(f"\t{n}\n", "\t1\n"))
        err(5, "\\t 和 \\n 转义不对");
    if (!eq(f"\"{n}\"", "\"1\""))
        err(6, "\\\" 转义不对");
    if (!eq(f"\\{n}", "\\1"))
        err(7, "\\\\ 转义不对");
    if (!eq(f"\x41\101", "AA"))
        err(8, "\\x 和八进制转义不对");
}

test "插值可以是任意表达式" {
    int i = 0;
    int a = 3, b = 4;

    if (!eq(f"{a + b}", "7"))
        err(1, "算术表达式不对");
    if (!eq(f"{i++}{i}", "01"))
        err(2, "各段从左到右求值");
    if (!eq(f"{a > b ? \"big\" : \"small\"}", "small"))
        err(3, "条件表达式不对");
    if (!eq(f"{(int)strlen(\"abcd\")}", "4"))
        err(4, "调用表达式不对");
    if (!eq(f"{-a}{+b}", "-34"))
        err(5, "一元运算符不对");
}

test "嵌套的 f\"..\"" {
    int n = 7;

    if (!eq(f"a{f\"b{n}c\"}d", "ab7cd"))
        err(1, "嵌套一层不对");
    if (!eq(f"x{f\"y{f\\\"z{n}\\\"}y\"}x", "xyz7yx"))
        err(2, "嵌套两层不对（每深一层多一个 '\\'）");
    if (!eq(f"{f\"{n}\"}", "7"))
        err(3, "嵌套里只有插值也不对");
}

test "空串和长文本" {
    char buf[400] = {0};
    int i = 0;
    const char *p = 0;

    if (!eq(f"", ""))
        err(1, "空 f\"\" 应该是空串");
    for (i = 0; i < 300; i++)
        buf[i] = 'x';
    p = f"[{buf}]";
    if (300 + 2 != (int)strlen(p))
        err(2, "长文本长度不对");
    if (p[0] != '[' || p[301] != ']' || p[150] != 'x')
        err(3, "长文本内容不对");
}

test "@f\"..\"：托管对象的生命周期跟着引用走" {
    int n = 3;
    @char p = 0;

    {
        p = @f"saved {n}";
    }
    if (!eq(p, "saved 3"))
        err(1, "出作用域后托管文本不该失效");
    if ((unsigned long long)strlen(p) != __c6_str_len(p))
        err(2, "__c6_str_len 给的文本长度不对");
    {
        shared @char s = @f"shared {n}";
        if (!eq(s, "shared 3"))
            err(3, "shared @char 存 @f 的结果不对");
    }
}

test "循环里的 @f\"..\" 每个都释放" {
    unsigned long long base = __c6_gc_live();
    int i = 0;

    for (i = 0; i < 50; i++) {
        @char p = @f"i={i}";
        if (p[0] != 'i' || p[1] != '=')
            err(1, "循环里的文本不对");
    }
    if (__c6_gc_live() != base)
        err(2, "循环结束后有托管对象没有释放");
}

test "普通 f\"..\" 借用本深度的临时缓冲" {
    int n = 1;
    const char *a = f"A{n}";
    const char *b = f"B{n}";

    if (!eq(b, "B1"))
        err(1, "第二个 f\"\" 不对");
    /* 借用只在"下一次同一个深度求值"之前有效，见 README */
    if (!eq(a, "B1"))
        err(2, "同一个深度应该复用同一块缓冲");
    if (!eq(f"ok {f\"in{n}\"}", "ok in1"))
        err(3, "更深的嵌套有自己的缓冲，不该覆盖外层的");
}

test "err(f\"..\") 的消息会被复制下来" {
    if (run_boom(9) != -1)
        err(1, "应该走进 catch");
    if (last_id != 9)
        err(2, "错误号不对");
    /* 消息来自另一个 TU 里的 @f".."：它已经随返回放开，留下来的必须是副本 */
    if (!eq(last_msg, "bad 9"))
        err(3, "err(f\"\") 的消息不对，或者已经被覆盖");
}

test "跨翻译单元" {
    if (2 != tu_text_len(f"{7}{8}"))
        err(1, "把 f\"\" 的结果传给另一个 TU 不对");
    if (3 != (int)strlen(f"{1}{2}{3}"))
        err(2, "f\"\" 直接当实参不对");
    if (!eq(mk_msg(5), "local5"))
        err(3, "函数返回 @f\"\" 不对");
}
