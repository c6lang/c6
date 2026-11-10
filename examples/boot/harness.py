#!/usr/bin/env python3
"""给一个 tests/*.c 生成裸机的测试入口。

tests/*.c 里的 `test "描述" { ... }` 会被编译成 `int __tcc_test_<i>(void)`，但
"有哪几个、什么顺序、叫什么名字"只存在编译器的状态里（tccrun.c 的 -test 用
的是那张表），链接出来的二进制里没有。所以这里按源码里的出现顺序把它们扫出来，
生成一张表和一个 `_start`，行为对着 tccrun 的 harness：

    __tcc_err_clear();  跑之前清错误槽
    r = fn();           0 = 通过
    __tcc_err_msg_get() 失败了拿 err() 的消息
    最后打印 "N test(s), M failure(s)"，用 boot_exit(失败数 ? 1 : 0) 收尾

    harness.py build/harness_fstr.c tests/fstr.c
    harness.py build/harness_module.c tests/module.c tests/tu/module.c

个别 test 块要的东西裸机上真没有（例：线程的 `h->join()` / `h->value()` 走
libtcc1 的每线程块，收尾会三重故障），这种块只要在 `test` 上一行写

    // BARE-SKIP

就从裸机的表里去掉：块号不变（还是 `__tcc_test_<n>`，只是没人调它），同一
个文件里剩下的块照跑 —— 见 tests/generic_fn.c。
"""
import re
import sys

TEST_RE = re.compile(r'^[ \t]*test[ \t]+"((?:[^"\\]|\\.)*)"', re.M)
SKIP_MARK = "BARE-SKIP"


def scan(src):
    """按出现顺序返回 [(描述, 裸机跳过?)]；下标就是 __tcc_test_<n> 的 n。"""
    lines = open(src, encoding="utf-8").read().split("\n")
    out = []
    for i, line in enumerate(lines):
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
