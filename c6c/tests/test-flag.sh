#!/bin/sh
# -test 的回归：跑 `test "…" { }` 块这条路的行为。
#
# 口径和 tcc 的 native 运行器一致（tests/run-tests.sh 的 1d2 那几条检查）：
#   * -test 下用户 main 不执行，每块一行 PASS/FAIL，收尾一行统计；
#   * -test-filter=子串 只跑描述里含该子串的块；
#   * 以 err 结束的块报 FAIL（带错误消息、走 stderr），进程退出码非 0；
#   * 空块算 PASS；零个块（或被过滤空了）时静默退出 0，和 tcc 一样；
#   * -test -o 只写文件不跑；多文件（多 TU）的块都登记进来；
#   * 和 -fsyntax-only/-c/-E/--emit-c/--emit-llvm 互斥，-nostdlib 不行。
set -u

C6C=${C6C:-./build/c6c}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
TMPDIR="$WORK/tmp"
export TMPDIR
mkdir -p "$TMPDIR"

pass=0
fail=0
ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1${2:+：$2}"; }

# 报告行里的描述不带引号、不带 "test:" 前缀——和 tcc 的 C6TestEntry.desc 一样
cat > "$WORK/blocks.c6" <<'EOF'
#include <stdio.h>

test "算术" {
    if (1 + 1 != 2)
        return err(1, "1 + 1 != 2");
}

test "空块也是 PASS" {
}

#if defined FAILING
test "这一块必须失败" {
    return err(7, "on purpose");
}
#endif

/* -test 下这段不能被执行 */
int main(void)
{
    printf("main should not run\n");
    return 3;
}
EOF

# 1) 全跑：每块一行 PASS、main 不执行、收尾统计在 stdout 最后一行
"$C6C" -test "$WORK/blocks.c6" >"$WORK/all.out" 2>"$WORK/all.err"
rc=$?
if [ "$rc" = 0 ]; then
    ok "全部通过时退出码 0"
else
    bad "全部通过时退出码" "期望 0，实际 $rc：$(cat "$WORK/all.err")"
fi
if grep -qx 'PASS: 算术' "$WORK/all.out" && grep -qx 'PASS: 空块也是 PASS' "$WORK/all.out"; then
    ok "PASS 行用描述本身（无引号、无 test: 前缀）"
else
    bad "PASS 行" "$(cat "$WORK/all.out")"
fi
if grep -q 'main should not run' "$WORK/all.out" "$WORK/all.err"; then
    bad "用户 main 不执行" "main 的输出出现了"
else
    ok "用户 main 不执行"
fi
if [ "$(tail -n 1 "$WORK/all.out")" = "2 test(s), 0 failure(s)" ]; then
    ok "收尾统计 2 test(s), 0 failure(s)"
else
    bad "收尾统计" "$(tail -n 1 "$WORK/all.out")"
fi
if [ -s "$WORK/all.err" ]; then
    bad "全部通过时 stderr 干净" "$(cat "$WORK/all.err")"
else
    ok "全部通过时 stderr 干净"
fi

# 2) 过滤：只跑匹配的块，块数按过滤后的算
out=$("$C6C" -test -test-filter=算术 "$WORK/blocks.c6" 2>"$WORK/err")
rc=$?
if [ "$rc" = 0 ] && [ "$(printf '%s\n' "$out" | grep -c '^PASS:')" = 1 ] \
   && printf '%s\n' "$out" | grep -qx 'PASS: 算术' \
   && [ "$(printf '%s\n' "$out" | tail -n 1)" = "1 test(s), 0 failure(s)" ]; then
    ok "-test-filter 只跑匹配的块"
else
    bad "-test-filter" "rc=$rc：$out"
fi
# 过滤空：静默、退出码 0（tcc 的 -test 也一样）
out=$("$C6C" -test -test-filter=不存在的名字 "$WORK/blocks.c6" 2>"$WORK/err")
rc=$?
if [ "$rc" = 0 ] && [ -z "$out" ] && [ ! -s "$WORK/err" ]; then
    ok "-test-filter 过滤空了就静默退出 0"
else
    bad "-test-filter 过滤空" "rc=$rc：$out"
fi

# 3) 以 err 结束的块：FAIL 行进 stderr、带错误消息、退出码非 0、失败计数对
"$C6C" -D FAILING -test "$WORK/blocks.c6" >"$WORK/f.out" 2>"$WORK/f.err"
rc=$?
if [ "$rc" != 0 ]; then
    ok "失败的块让进程以非 0 退出"
else
    bad "失败的块" "退出码还是 0"
fi
if grep -qx 'FAIL: 这一块必须失败: on purpose' "$WORK/f.err"; then
    ok "FAIL 行带错误消息"
else
    bad "FAIL 行" "$(cat "$WORK/f.err")"
fi
if [ "$(tail -n 1 "$WORK/f.out")" = "3 test(s), 1 failure(s)" ]; then
    ok "收尾统计 3 test(s), 1 failure(s)"
else
    bad "失败计数" "$(tail -n 1 "$WORK/f.out")"
fi

# 4) 零个块：静默退出 0（以前会报"没有 main"）
cat > "$WORK/notests.c6" <<'EOF'
int main(void)
{
    return 3;
}
EOF
out=$("$C6C" -test "$WORK/notests.c6" 2>"$WORK/err")
rc=$?
if [ "$rc" = 0 ] && [ -z "$out" ] && [ ! -s "$WORK/err" ]; then
    ok "没有 test 块时静默退出 0（用户 main 也不跑）"
else
    bad "没有 test 块" "rc=$rc：$out"
fi

# 5) -test -o：只写文件不跑，写出来的程序跑起来仍然是同一份报告
cat > "$WORK/notests2.c6" <<'EOF'
test "from tu" {
}
EOF
if "$C6C" -test -o "$WORK/blocks" "$WORK/blocks.c6" >"$WORK/o.log" 2>"$WORK/o.err"; then
    if grep -q '^PASS:' "$WORK/o.log"; then
        bad "-test -o 只写文件" "编译期就打报告了：$(cat "$WORK/o.log")"
    else
        out=$("$WORK/blocks" 2>&1)
        if printf '%s\n' "$out" | grep -qx 'PASS: 算术' \
           && [ "$(printf '%s\n' "$out" | tail -n 1)" = "2 test(s), 0 failure(s)" ]; then
            ok "-test -o 只写文件，跑起来仍是同一份报告"
        else
            bad "-test -o 产物" "$out"
        fi
    fi
else
    bad "-test -o" "$(cat "$WORK/o.err")"
fi

# 6) 多文件（多 TU）：命令行上所有文件的块都登记，按出现顺序跑
"$C6C" -test "$WORK/blocks.c6" "$WORK/notests2.c6" >"$WORK/tu.out" 2>"$WORK/tu.err"
rc=$?
if [ "$rc" = 0 ] && [ "$(tail -n 1 "$WORK/tu.out")" = "3 test(s), 0 failure(s)" ] \
   && grep -qx 'PASS: from tu' "$WORK/tu.out"; then
    ok "多 TU 的块一起登记"
else
    bad "多 TU" "rc=$rc：$(cat "$WORK/tu.out")$(cat "$WORK/tu.err")"
fi

# 7) 模式互斥：-test 是"编完就跑"，和只出文件/只查错的模式不能一起用
for badflag in -fsyntax-only -c -E --emit-c --emit-llvm -nostdlib; do
    "$C6C" -test "$badflag" -o "$WORK/x" "$WORK/blocks.c6" >"$WORK/m.out" 2>"$WORK/m.err"
    rc=$?
    if [ "$rc" = 2 ]; then
        ok "-test 与 $badflag 互斥"
    else
        bad "-test 与 $badflag 互斥" "退出码 $rc（应为 2）：$(cat "$WORK/m.err")"
    fi
done

echo "-test 回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
