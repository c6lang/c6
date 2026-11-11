#!/usr/bin/env python3
"""给一个 tests/*.c6 生成裸机的测试入口。

tests/*.c6 里的 `test "描述" { ... }` 会被编译成 `int __tcc_test_<i>(void)`，但
"有哪几个、什么顺序、叫什么名字"只存在编译器的状态里（tccrun.c 的 -test 用
的是那张表），链接出来的二进制里没有。所以这里按源码里的出现顺序把它们扫出来，
生成一张表和一个 `_start`，行为对着 tccrun 的 harness：

    __tcc_err_clear();  跑之前清错误槽
    r = fn();           0 = 通过
    __tcc_err_msg_get() 失败了拿 err() 的消息
    最后打印 "N test(s), M failure(s)"，用 boot_exit(失败数 ? 1 : 0) 收尾

    harness.py build/harness_fstr.c tests/fstr.c6
    harness.py build/harness_module.c tests/module.c6 tests/tu/module.c6

个别 test 块要的东西裸机上真没有（例：线程的 `h->join()` / `h->value()` 走
libtcc1 的每线程块，收尾会三重故障），这种块只要在 `test` 上一行写

    // BARE-SKIP

就从裸机的表里去掉：块号不变（还是 `__tcc_test_<n>`，只是没人调它），同一
个文件里剩下的块照跑 —— 见 tests/generic_fn.c6。

条件编译里**肯定没开**的块是另一回事：它在编译器那张表里压根不存在，所以既不
进裸机的表，也不占块号（`tests/test-block.c6` 里藏 `#if defined FAILING_BLOCK`
的那一块：裸机编译不带 `-D`，它不注册，块号要让给后面的块，否则会去引用一个
不存在的 `__tcc_test_<n>`，链接期报 unresolved reference）。判断不了的
`#if`（比较表达式、别的宏）一律当它开着 —— 漏一个块是响的链接错误，多扫一个
块却是静默少跑，宁可要前者。
"""
import re
import sys

TEST_RE = re.compile(r'^[ \t]*test[ \t]+"((?:[^"\\]|\\.)*)"', re.M)
SKIP_MARK = "BARE-SKIP"
COND_RE = re.compile(r'^[ \t]*#[ \t]*(ifdef|ifndef|if|elif|else|endif)\b(.*)$')
DEFINED_RE = re.compile(r'(!?)[ \t]*defined[ \t]*(?:\([ \t]*(\w+)[ \t]*\)|(\w+))$')


def cond_value(kind, expr, defines=()):
    """一条 #if / #ifdef / #ifndef 成不成立：True / False / None（判断不了）。

    裸机编译不给任何 -D（见 examples/boot/build.sh），所以 defines 默认是空的，
    `defined X` 一律是假。
    """
    expr = expr.strip()
    if kind in ("ifdef", "ifndef"):
        name = expr.split()[0] if expr.split() else ""
        if not name:
            return None
        return (name in defines) if kind == "ifdef" else (name not in defines)
    if expr in ("0", "1"):
        return expr == "1"
    m = DEFINED_RE.match(expr)
    if m:
        name = m.group(2) or m.group(3)
        want = not m.group(1)                   # '!defined X' 取反
        return (name in defines) == want
    return None


def scan(src, defines=()):
    """按出现顺序返回 [(描述, 裸机跳过?)]；下标就是 __tcc_test_<n> 的 n。"""
    lines = open(src, encoding="utf-8").read().split("\n")
    out = []
    cond = []                                   # 每层 #if 的真值：True/False/None
    for i, line in enumerate(lines):
        m = COND_RE.match(line)
        if m:
            kind, expr = m.group(1), m.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                cond.append(cond_value(kind, expr, defines))
            elif kind == "else":
                if cond:
                    cond[-1] = None if cond[-1] is None else not cond[-1]
            elif kind == "elif":
                if cond:                        # 判断不了就当这一支开着
                    cond[-1] = None
            elif cond:                          # endif
                cond.pop()
            continue
        if any(c is False for c in cond):       # 这块不在编译结果里：不占块号
            continue
        m = TEST_RE.match(line)
        if not m:
            continue
        skip = False
        for back in range(i - 1, -1, -1):       # 往前只看空行和 // 注释
            s = lines[back].strip()
            if not s:
                continue
            if not s.startswith("//"):
                break
            if SKIP_MARK in s:
                skip = True
        out.append((m.group(1), skip))
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit("用法: harness.py <生成的 C 文件> <测试源>...")
    dst = sys.argv[1]
    blocks = []
    for src in sys.argv[2:]:
        blocks += scan(src)
    if not blocks:
        sys.exit("没有 test 块")
    descs = [(n, d) for n, (d, skip) in enumerate(blocks) if not skip]
    nskip = len(blocks) - len(descs)
    if not descs:
        sys.exit("所有 test 块都标了 BARE-SKIP")

    out = []
    out.append("/* 自动生成（examples/boot/harness.py）：裸机版 -test harness */\n")
    out.append("void boot_exit(int code);\n")
    out.append("int printf(const char *fmt, ...);\n")
    out.append("int __tcc_err_clear(void);\n")
    out.append("const char *__tcc_err_msg_get(void);\n")
    out.append("void __c6_th_join_all(void);\n\n")
    for n, d in descs:
        out.append("int __tcc_test_%d(void);\n" % n)
    out.append("\nstruct __c6_test_ent {\n    int (*fn)(void);\n"
               "    const char *desc;\n};\n\n")
    out.append("static struct __c6_test_ent __c6_test_tbl[] = {\n")
    for n, d in descs:
        out.append('    { __tcc_test_%d, "%s" },\n' % (n, d))
    out.append("};\n\n")
    out.append("""void _start(void)
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
            printf("\\n");
        } else {
            printf("PASS: %s\\n", __c6_test_tbl[i].desc);
        }
    }
    /* spawn 起的线程必须先收干净，运行时代码才会被释放 */
    __c6_th_join_all();
    printf("%d test(s), %d failure(s)\\n", n, nfail);
    boot_exit(nfail ? 1 : 0);
}
""")
    open(dst, "w", encoding="utf-8").write("".join(out))
    print("%d %d" % (len(descs), nskip))


main()
