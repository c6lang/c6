#!/bin/sh
# -run 的回归：编到临时 ELF 再 exec 这条路的行为。
# 覆盖退出码、参数透传（argv[0] 是源文件名）、test 块、临时文件清理、
# 与别的输出模式/`-o` 的互斥，以及和 `-o` 后直接执行的结果一致。
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

# 1) 退出码照抄，成功时编译器自己不出声
cat > "$WORK/exit7.c6" <<'EOF'
int main(void)
{
    return 7;
}
EOF
"$C6C" -run "$WORK/exit7.c6" >"$WORK/out" 2>"$WORK/err"
rc=$?
if [ "$rc" = 7 ]; then
    ok "退出码 7"
else
    bad "退出码" "期望 7，实际 $rc：$(cat "$WORK/err")"
fi
if [ -s "$WORK/out" ]; then
    bad "成功时不刷屏" "stdout 有内容：$(cat "$WORK/out")"
else
    ok "成功时不刷屏"
fi

# 2) 参数原样传进去；argv[0] 就是命令行上那个源文件名
cat > "$WORK/args.c6" <<'EOF'
#include <stdio.h>

int main(int argc, char **argv)
{
    int i = 0;

    printf("argc=%d", argc);
    for (i = 0; i < argc; i++)
        printf(" [%s]", argv[i]);
    printf("\n");
    return 0;
}
EOF
"$C6C" -run "$WORK/args.c6" one -two >"$WORK/args.out" 2>"$WORK/err"
want="argc=3 [$WORK/args.c6] [one] [-two]"
got=$(cat "$WORK/args.out")
if [ "$got" = "$want" ]; then
    ok "参数与 argv[0]"
else
    bad "参数与 argv[0]" "期望 '$want'，实际 '$got'"
fi

# 3) 只有 test 块、没有 main 的文件：合成入口跑测试，失败数当退出码。
#    （c6c 的运行器只在失败时打 `FAIL …`；没有 tcc -test 那行汇总。）
cat > "$WORK/t.c6" <<'EOF'
test "run 里的 test 块" {
    int a[2] = { 1, 2 };

    if (a[0] + a[1] != 3)
        return 1;
}
EOF
"$C6C" -run "$WORK/t.c6" >"$WORK/t.out" 2>"$WORK/err"
rc=$?
if [ "$rc" = 0 ] && [ ! -s "$WORK/t.out" ]; then
    ok "test 块（全过）"
else
    bad "test 块（全过）" "退出码 $rc，输出：$(cat "$WORK/t.out")$(cat "$WORK/err")"
fi

cat > "$WORK/tbad.c6" <<'EOF'
test "会挂的块 A" {
    return 1;
}

test "会挂的块 B" {
    return 2;
}
EOF
"$C6C" -run "$WORK/tbad.c6" >"$WORK/tbad.out" 2>"$WORK/err"
rc=$?
if [ "$rc" = 2 ] && grep -q '^FAIL test:"会挂的块 A"$' "$WORK/tbad.out" \
   && grep -q '^FAIL test:"会挂的块 B"$' "$WORK/tbad.out"; then
    ok "test 块（失败数当退出码）"
else
    bad "test 块（失败数当退出码）" "退出码 $rc（期望 2），输出：$(cat "$WORK/tbad.out")$(cat "$WORK/err")"
fi

# 4) 被信号杀：按 shell 惯例 128+sig
cat > "$WORK/sig.c6" <<'EOF'
int raise(int sig);

int main(void)
{
    raise(15);
    return 0;
}
EOF
"$C6C" -run "$WORK/sig.c6" >/dev/null 2>"$WORK/err"
rc=$?
if [ "$rc" = 143 ]; then
    ok "信号退出码 128+15"
else
    bad "信号退出码" "期望 143，实际 $rc：$(cat "$WORK/err")"
fi

# 5) shebang：c6c 可以直接当 C 脚本的解释器（首行 `#!` 被当作空白）
C6C_ABS=$(cd "$(dirname "$C6C")" && pwd)/$(basename "$C6C")
cat > "$WORK/script.c6" <<EOF
#!$C6C_ABS -run
#include <stdio.h>
int main(int argc, char **argv)
{
    printf("script argc=%d arg=%s\n", argc, argc > 1 ? argv[1] : "-");
    return 3;
}
EOF
chmod +x "$WORK/script.c6"
"$WORK/script.c6" hi >"$WORK/script.out" 2>"$WORK/err"
rc=$?
if [ "$rc" = 3 ] && grep -q 'script argc=2 arg=hi' "$WORK/script.out"; then
    ok "shebang 脚本"
else
    bad "shebang 脚本" "退出码 $rc，输出：$(cat "$WORK/script.out")$(cat "$WORK/err")"
fi

# 6) 临时文件必须清干净（TMPDIR 就是专门为这一条准备的）
left=$(ls -A "$TMPDIR" 2>/dev/null)
if [ -z "$left" ]; then
    ok "临时文件清理"
else
    bad "临时文件清理" "残留：$left"
fi

# 7) 编译失败：非零退出、同样不留临时文件
cat > "$WORK/bad.c6" <<'EOF'
int main(void)
{
    return 0
}
EOF
"$C6C" -run "$WORK/bad.c6" >/dev/null 2>"$WORK/err"
rc=$?
left=$(ls -A "$TMPDIR" 2>/dev/null)
if [ "$rc" != 0 ] && [ -z "$left" ]; then
    ok "编译失败"
else
    bad "编译失败" "退出码 $rc，残留：$left"
fi

# 8) 和别的输出模式 / -o 互斥
check_reject() {
    name=$1
    shift
    "$C6C" "$@" >/dev/null 2>"$WORK/err"
    rc=$?
    if [ "$rc" = 2 ]; then
        ok "$name"
    else
        bad "$name" "退出码 $rc（期望 2）：$(cat "$WORK/err")"
    fi
}
check_reject "-run 与 -o 互斥" -run -o "$WORK/x" "$WORK/exit7.c6"
check_reject "-run 与 -c 互斥（-run 在后）" -c -run "$WORK/exit7.c6"
check_reject "-run 与 -c 互斥（-c 在后）" -run -c "$WORK/exit7.c6"
check_reject "-run 与 -fsyntax-only 互斥" -run -fsyntax-only "$WORK/exit7.c6"
check_reject "-run 缺源文件" -run

# 9) 和 `-o` + 直接执行同构：同一份源码、同一个退出码
"$C6C" "$WORK/exit7.c6" -o "$WORK/exit7" >/dev/null 2>"$WORK/err"
"$WORK/exit7"
rc=$?
if [ "$rc" = 7 ]; then
    ok "-o 后直接执行（对照）"
else
    bad "-o 后直接执行（对照）" "期望 7，实际 $rc：$(cat "$WORK/err")"
fi

echo "-run 回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
