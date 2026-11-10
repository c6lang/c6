#!/bin/sh
# c6 自测运行器：跑 tests/ 下的所有 c6 测试。
#
#   tests/run-tests.sh                 跑全部
#   tests/run-tests.sh defer err       只跑这几个（按文件名，可写 errors/xxx）
#   tests/run-tests.sh --list          列出会跑的文件
#   BACKEND=c99 tests/run-tests.sh     同一批用例改走 C99 输出后端
#
# 环境变量：
#   TCC       编译器路径（默认 <仓库>/tcc/tcc）
#   TCCFLAGS  追加给编译器的参数
#   BACKEND   native（默认）用 tcc/tcc 直接编译运行；c99 让同一个 tcc
#             用 -o *.c / -o <目录> 生成 C 源码，再用系统 C 编译器编译运行。
#             两种模式跑完全相同的测试文件和断言。
#   C99TCC    产出 C 源码的编译器（默认 <仓库>/tcc/tcc）
#   C99CC     编译生成代码的系统 C 编译器（默认 gcc）
#   C99FLAGS  交给 C99CC 的参数（默认 ISO C99 + 严格告警）；把 C99CC 换成
#             tcc 时用它去掉 gcc 专有的开关，例如 "-B <仓库>/tcc -std=c99 -w"
#             （见 make c99tcc-test）
#
# 测试分组（所有 c6 代码都在 .c6 文件里：后缀是唯一的开关，命令行没有
# -std=c6 这个选项）：
#   tests/*.c6        用 -test 跑里面的 test 块；若存在同名的 tests/tu/<name>.c6，
#                     会一起编译，作为第二个翻译单元。
#   tests/c6ext/*.c6  后缀自测：同一个编译器、不加任何开关，'.c6' 自己开启扩展；
#                     同名 tests/tu/<name>.c6 作为第二个 TU，构建时带
#                     '-I tests'，让 'import tu.xxx' 的点号路径能找到模块。
#   tests/errors/*.c6 编译期诊断；每个 `// EXPECT <片段>: <诊断>` 注释对应一次
#                     -D<片段> 编译，要求 stderr 含该诊断；不带 -D 时该文件
#                     必须能编译并运行通过。c99 模式下优先使用
#                     `// EXPECT-C99 <片段>: <诊断>`，没有时才回退到 EXPECT。
#                     `// FLAGS ...` 行给出只属于这个文件的编译参数（例如
#                     '-Wgeneric-depth' 这类只管告警的开关）。
#   tests/exit/*.c6   退出码；`// EXIT <n>` 注释声明期望的进程退出码，编译成
#                     可执行文件后运行，比对退出码（main 的返回值必须穿过
#                     c6 的清理链）。
#   tests/abi/*.c6    ABI 兼容；每个文件按 c6 编译（.c6 后缀），同名的
#                     _plain.c 是纯 C 文件（.c 后缀），两个目标文件链接后运行，
#                     比对 `// EXIT <n>` 声明的退出码。
#   tests/c99/*.c6    C99 输出后端；每个文件先用 tcc/tcc 生成 C
#                     源码，再用 gcc -std=c99 编译并运行，比对
#                     `// EXIT <n>`（默认 0）；带 `// OUTPUT <文本>` 时
#                     还要求 stdout 含该文本。
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TCC=${TCC:-$ROOT/tcc/tcc}
TCCFLAGS=${TCCFLAGS:-}
BASE="-B $ROOT/tcc -I $ROOT/tests"
BACKEND=${BACKEND:-native}
C99TCC=${C99TCC:-$ROOT/tcc/tcc}
C99CC=${C99CC:-gcc}
C99FLAGS=${C99FLAGS:-"-std=c99 -pedantic -Wall -Wextra -Werror -pthread"}

case $BACKEND in
native) ;;
c99) ;;
*)
    echo "run-tests: 未知的 BACKEND='$BACKEND'（native 或 c99）" >&2
    exit 1
    ;;
esac

if [ "$BACKEND" = native ] && [ ! -x "$TCC" ]; then
    echo "run-tests: 找不到 $TCC，先执行 'make build'" >&2
    exit 1
fi
if [ "$BACKEND" = c99 ]; then
    if [ ! -x "$C99TCC" ]; then
        echo "run-tests: 找不到 $C99TCC，先执行 'make build'" >&2
        exit 1
    fi
    if ! command -v "$C99CC" >/dev/null 2>&1; then
        echo "run-tests: 找不到系统 C 编译器 $C99CC" >&2
        exit 1
    fi
fi

TMP=${TMPDIR:-/tmp}/c6-tests.$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

npass=0
nfail=0
failed_list=

fail() {
    nfail=$((nfail + 1))
    failed_list="$failed_list $1"
    echo "FAIL: $1" >&2
    shift
    [ $# -eq 0 ] || printf '%s\n' "$@" | sed 's/^/      /' >&2
    return 0
}

note_pass() {
    npass=$((npass + 1))
    echo "ok   $1"
}

# 用当前后端把源文件编成可执行文件，成功返回 0。
# c99 模式一次调用可以带多个源文件：tcc 把它们合成为一个 C 文件，
# 这样多翻译单元的程序仍然只有一个合成的 main / 一份测试入口。
build_exe() { # <可执行文件> <源文件>...
    exe=$1
    shift
    if [ "$BACKEND" = c99 ]; then
        $C99TCC -B "$ROOT/tcc" -I "$ROOT/tests" $TCCFLAGS \
            -o "$exe.c" "$@" || return 1
        $C99CC $C99FLAGS -o "$exe" "$exe.c"
    else
        $TCC $BASE $TCCFLAGS -o "$exe" "$@"
    fi
}

# 用 -test 模式编译并运行 test 块（源文件里的 main 不执行）。
# 两种后端都把测试报告打到 stdout，返回测试进程的退出码。
build_test_exe() { # <可执行文件> <源文件>...
    exe=$1
    shift
    if [ "$BACKEND" = c99 ]; then
        # c99 后端里 -test 直接生成带测试入口的 C 源码
        $C99TCC -test -B "$ROOT/tcc" -I "$ROOT/tests" $TCCFLAGS \
            -o "$exe.c" "$@" || return 1
        $C99CC $C99FLAGS -o "$exe" "$exe.c" || return 1
        "$exe"
    else
        $TCC $BASE $TCCFLAGS -test "$@"
    fi
}

# 用户点名要跑的测试（去掉 .c/.c6 后缀的纯名字），空表示全跑
wanted=
if [ "${1:-}" = "--list" ]; then
    for f in "$ROOT"/tests/*.c6; do
        echo "$(basename "$f" .c6)"
    done
    for f in "$ROOT"/tests/c6ext/*.c6; do
        [ -e "$f" ] && echo "c6ext/$(basename "$f" .c6)"
    done
    for f in "$ROOT"/tests/errors/*.c6; do
        [ -e "$f" ] && echo "errors/$(basename "$f" .c6)"
    done
    for f in "$ROOT"/tests/exit/*.c6; do
        [ -e "$f" ] && echo "exit/$(basename "$f" .c6)"
    done
    for f in "$ROOT"/tests/abi/*.c6; do
        [ -e "$f" ] && echo "abi/$(basename "$f" .c6)"
    done
    for f in "$ROOT"/tests/c99/*.c6; do
        [ -e "$f" ] && echo "c99/$(basename "$f" .c6)"
    done
    exit 0
fi
for a in "$@"; do
    wanted="$wanted $(echo "$a" | sed 's,\.c6$,,; s,\.c$,,; s,^\./,,; s,^tests/,,')"
done

is_wanted() {
    [ -z "$wanted" ] && return 0
    for w in $wanted; do
        case $1 in
        *"$w" | "$w" | "$w"/*) return 0 ;;
        esac
    done
    return 1
}

# ---- 1. test 块：c6 代码都在 .c6 文件里，后缀自己开启扩展 ----
for src in "$ROOT"/tests/*.c6; do
    name=$(basename "$src" .c6)
    is_wanted "$name" || continue
    tu="$ROOT/tests/tu/$name.c6"
    out=
    if [ -e "$tu" ]; then
        set -- "$src" "$tu"
    else
        set -- "$src"
    fi
    if out=$(build_test_exe "$TMP/tests.bin" "$@" 2>"$TMP/err") \
       && echo "$out" | tail -n 1 | grep -q ', 0 failure(s)$'; then
        note_pass "$name: $(echo "$out" | tail -n 1)"
    else
        fail "$name" "$(cat "$TMP/err")" \
             "$(echo "$out" | grep -E '^(FAIL|[0-9]+ test)')"
    fi
done

# ---- 1b. 后缀自测：不加任何开关，'.c6' 自己开启 c6 ----
for src in "$ROOT"/tests/c6ext/*.c6; do
    [ -e "$src" ] || continue
    name="c6ext/$(basename "$src" .c6)"
    is_wanted "$name" || continue
    tu="$ROOT/tests/tu/$(basename "$src" .c6).c6"
    out=
    if [ -e "$tu" ]; then
        set -- "$src" "$tu"
    else
        set -- "$src"
    fi
    if out=$(build_test_exe "$TMP/c6ext.bin" "$@" 2>"$TMP/err") \
       && echo "$out" | tail -n 1 | grep -q ', 0 failure(s)$'; then
        note_pass "$name: $(echo "$out" | tail -n 1)"
    else
        fail "$name" "$(cat "$TMP/err")" \
             "$(echo "$out" | grep -E '^(FAIL|[0-9]+ test)')"
    fi
done

# ---- 1c. -run：.c6 后缀同样开启 c6（用户最常见的用法，仅本机后端） ----
if [ "$BACKEND" = native ] && is_wanted "c6ext/run"; then
    cat > "$TMP/run.c6" <<'EOF'
int main(void)
{
    int a[3] = { 1, 2, 3 };
    int[:] s = a;
    if (s[1] != 2)
        return 1;
    return 7;
}
EOF
    $TCC -B "$ROOT/tcc" $TCCFLAGS -run "$TMP/run.c6" \
        >/dev/null 2>"$TMP/err"
    got=$?
    if [ "$got" = 7 ]; then
        note_pass "c6ext/run: -run 退出码 7"
    else
        fail "c6ext/run" "退出码 $got，期望 7" "$(cat "$TMP/err")"
    fi
fi

# ---- 1d. 后缀是唯一开关：.c 里写 c6 语法要报错，-std=c6 也要报错 ----
if [ "$BACKEND" = native ] && is_wanted "c6ext/rule"; then
    ok=yes
    detail=

    # c6 语法放在 .c 里：编译失败，且诊断指向后缀
    printf '%s\n' 'int main(void)' '{' '    defer { }' '    return 0;' '}' \
        > "$TMP/plain.c"
    if $TCC -B "$ROOT/tcc" $TCCFLAGS -run "$TMP/plain.c" \
            >/dev/null 2>"$TMP/err"; then
        ok=no
        detail="$detail
.c 里的 defer 竟然编过了"
    elif ! grep -qF "requires a .c6 source file" "$TMP/err"; then
        ok=no
        detail="$detail
.c 里的 defer 没有报 'requires a .c6 source file':$(cat "$TMP/err")"
    fi

    # 同一段代码放进 .c6 就通过
    cp "$TMP/plain.c" "$TMP/plain.c6"
    if ! $TCC -B "$ROOT/tcc" $TCCFLAGS -run "$TMP/plain.c6" \
            >/dev/null 2>"$TMP/err"; then
        ok=no
        detail="$detail
.c6 里的 defer 编不过:$(cat "$TMP/err")"
    fi

    # -std=c6 已经取消：报错并让用户改后缀，而不是静默忽略
    if $TCC -B "$ROOT/tcc" $TCCFLAGS -std=c6 -run "$TMP/plain.c6" \
            >/dev/null 2>"$TMP/err"; then
        ok=no
        detail="$detail
-std=c6 竟然被接受了"
    elif ! grep -qF ".c6" "$TMP/err"; then
        ok=no
        detail="$detail
-std=c6 的报错没有提到 .c6:$(cat "$TMP/err")"
    fi

    if [ $ok = yes ]; then
        note_pass "c6ext/rule: .c 不开启扩展、-std=c6 报错"
    else
        fail "c6ext/rule" "$detail"
    fi
fi

# ---- 1e. 模块名就是它的路径：分级查找，扁平的 'a.b.c6' 不接受 ----
if [ "$BACKEND" = native ] && is_wanted "c6ext/modpath"; then
    ok=yes
    detail=
    mp="$TMP/mp"
    rm -rf "$mp"; mkdir -p "$mp/a"

    # 分级布局：module a.b; 放在 a/b.c6，import a.b; 按路径找到它
    printf 'module a.b;\nint answer(void) { return 42; }\n' > "$mp/a/b.c6"
    printf 'import a.b;\nint main(void) { return answer(); }\n' > "$mp/main.c6"
    if ! $TCC -B "$ROOT/tcc" $TCCFLAGS -o "$mp/app" "$mp/main.c6" "$mp/a/b.c6" \
            2>"$TMP/err"; then
        ok=no
        detail="$detail
分级布局编不过:$(cat "$TMP/err")"
    else
        "$mp/app"; got=$?
        if [ "$got" != 42 ]; then
            ok=no
            detail="$detail
分级布局运行退出码 $got，期望 42"
        fi
    fi

    # 只有扁平的 'a.b.c6'：拒绝，并说出它该去哪儿
    mv "$mp/a/b.c6" "$mp/a.b.c6"
    if $TCC -B "$ROOT/tcc" $TCCFLAGS -c -o "$mp/x.o" "$mp/main.c6" 2>"$TMP/err"; then
        ok=no
        detail="$detail
扁平布局竟然编过了"
    elif ! grep -qF "must live at 'a/b.c6'" "$TMP/err"; then
        ok=no
        detail="$detail
扁平布局没有指出应有的路径:$(cat "$TMP/err")"
    fi

    # 两种布局并存：分级路径胜出，import 解析不报错
    printf 'module a.b;\nint answer(void) { return 42; }\n' > "$mp/a/b.c6"
    if ! $TCC -B "$ROOT/tcc" $TCCFLAGS -c -o "$mp/x.o" "$mp/main.c6" 2>"$TMP/err"; then
        ok=no
        detail="$detail
并存时分级路径没有胜出:$(cat "$TMP/err")"
    fi
    rm -f "$mp/a.b.c6"

    # 名字和声明不一致：文件在 a/b.c6，却声明成 module a.zzz;
    printf 'module a.zzz;\nint answer(void) { return 1; }\n' > "$mp/a/b.c6"
    if $TCC -B "$ROOT/tcc" $TCCFLAGS -o "$mp/app3" "$mp/main.c6" "$mp/a/b.c6" \
            2>"$TMP/err"; then
        ok=no
        detail="$detail
声明和路径不一致竟然编过了"
    elif ! grep -qF "module file declares" "$TMP/err"; then
        ok=no
        detail="$detail
不一致没有报 'module file declares':$(cat "$TMP/err")"
    fi
    printf 'module a.b;\nint answer(void) { return 42; }\n' > "$mp/a/b.c6"

    # 找不到模块时，诊断要说出它期望的路径
    printf 'import nope.missing;\nint main(void) { return 0; }\n' > "$mp/miss.c6"
    if $TCC -B "$ROOT/tcc" $TCCFLAGS -run "$mp/miss.c6" 2>"$TMP/err"; then
        ok=no
        detail="$detail
找不到模块竟然编过了"
    elif ! grep -qF "expected a file at 'nope/missing.c6'" "$TMP/err"; then
        ok=no
        detail="$detail
找不到模块没有说出期望路径:$(cat "$TMP/err")"
    fi

    if [ $ok = yes ]; then
        note_pass "c6ext/modpath: 模块按路径分级、扁平文件被拒绝"
    else
        fail "c6ext/modpath" "$detail"
    fi
fi

# ---- 2. 编译期诊断 ----
for src in "$ROOT"/tests/errors/*.c6; do
    [ -e "$src" ] || continue
    name="errors/$(basename "$src" .c6)"
    is_wanted "$name" || continue

    # 同名的一半放在 tests/tu/ 时可以带第二个翻译单元一起编译（跨 TU 的
    # 环检测这类诊断要两个单元才看得见）
    extra=
    tu_src="$ROOT/tests/tu/$(basename "$src")"
    [ -e "$tu_src" ] && extra="$tu_src"

    # 该文件专属的编译参数（只管告警的开关之类）
    eflags=$(sed -n 's,^// *FLAGS *\(.*\)$,\1,p' "$src")

    # 期望表：片段名 <TAB> 诊断文本
    sed -n 's,^// *EXPECT *\([A-Za-z0-9_]*\): *\(.*\)$,\1\t\2,p' "$src" > "$TMP/expect"
    sed -n 's,^// *EXPECT-C99 *\([A-Za-z0-9_]*\): *\(.*\)$,\1\t\2,p' "$src" > "$TMP/expect-c99"
    # 只适用于本机后端的片段（C99 后端没有链接期检查之类）
    sed -n 's,^// *NATIVE-ONLY *\([A-Za-z0-9_]*\).*$,\1,p' "$src" > "$TMP/native-only"
    frags=$(cut -f1 "$TMP/expect" | awk '!seen[$0]++')

    ok=yes
    detail=

    # 2a. 每个片段都必须报出声明的全部诊断
    for frag in $frags; do
        if [ "$BACKEND" = c99 ] \
           && grep -qx "$frag" "$TMP/native-only"; then
            continue                # 这个片段只在本机后端成立
        fi
        if [ "$BACKEND" = c99 ] \
           && awk -F'\t' -v f="$frag" '$1 == f { found = 1 } END { exit !found }' \
                  "$TMP/expect-c99"; then
            expect_file="$TMP/expect-c99"
        else
            expect_file="$TMP/expect"
        fi

        if [ "$BACKEND" = c99 ]; then
            if $C99TCC -B "$ROOT/tcc" $TCCFLAGS $eflags \
                    -D"$frag" -o "$TMP/diag.c" "$src" $extra \
                    2>"$TMP/err" >/dev/null; then
                # 生成成功不等于有错：没有机器码链接步骤的未解析引用，
                # 要由系统 C 编译器在链接时才会报出来
                if $C99CC $C99FLAGS -o "$TMP/diag.bin" "$TMP/diag.c" \
                        2>>"$TMP/err" >/dev/null; then
                    ok=no
                    detail="$detail
-D$frag: 没有报错"
                fi
            fi
        elif [ -n "$extra" ]; then
            # 有两个单元时必须走真正的编译+链接（-run 只接一个源文件）
            if build_exe "$TMP/diag.bin" $eflags "$src" $extra \
                    -D"$frag" 2>"$TMP/err" >/dev/null; then
                ok=no
                detail="$detail
-D$frag: 没有报错"
            fi
        else
            if $TCC $BASE $TCCFLAGS $eflags -D"$frag" -run "$src" \
                    2>"$TMP/err" >/dev/null; then
                ok=no
                detail="$detail
-D$frag: 没有报错"
            fi
        fi
        awk -F'\t' -v f="$frag" '$1 == f { print substr($0, length($1) + 2) }' \
            "$expect_file" |
        while IFS= read -r w; do
            [ -z "$w" ] && continue
            grep -qF -- "$w" "$TMP/err" || printf '%s\n' "-D$frag: 缺少诊断: $w"
        done > "$TMP/missing"
        if [ -s "$TMP/missing" ]; then
            ok=no
            detail="$detail
$(cat "$TMP/missing")"
        fi
    done

    # 2b. 不带 -D 时必须能编译并运行通过
    if ! out=$(build_exe "$TMP/errors.bin" $eflags "$src" $extra 2>"$TMP/err" && "$TMP/errors.bin" 2>>"$TMP/err"); then
        ok=no
        detail="$detail
正常路径编译或运行失败:$(cat "$TMP/err")$out"
    fi

    if [ $ok = yes ]; then
        note_pass "$name: $(echo "$frags" | wc -w | tr -d ' ') 个诊断片段"
    else
        fail "$name" "$detail"
    fi
done

# ---- 3. 退出码 ----
for src in "$ROOT"/tests/exit/*.c6; do
    [ -e "$src" ] || continue
    name="exit/$(basename "$src" .c6)"
    is_wanted "$name" || continue
    want=$(sed -n 's,^// *EXIT *\([0-9][0-9]*\)$,\1,p' "$src" | head -n 1)
    if [ -z "$want" ]; then
        fail "$name" "缺少 '// EXIT <n>' 注释"
        continue
    fi
    if ! build_exe "$TMP/exit.bin" "$src" 2>"$TMP/err" >/dev/null; then
        fail "$name" "$(cat "$TMP/err")"
        continue
    fi
    "$TMP/exit.bin" >/dev/null 2>"$TMP/err"
    got=$?
    if [ "$got" = "$want" ]; then
        note_pass "$name: 退出码 $got"
    else
        fail "$name" "退出码 $got，期望 $want"
    fi
done

# ---- 4. ABI 兼容：c6 翻译单元（.c6）+ 纯 C 翻译单元（_plain.c）分开编译再链接 ----
for src in "$ROOT"/tests/abi/*.c6; do
    [ -e "$src" ] || continue
    name="abi/$(basename "$src" .c6)"
    is_wanted "$name" || continue
    plain="${src%.c6}_plain.c"
    want=$(sed -n 's,^// *EXIT *\([0-9][0-9]*\)$,\1,p' "$src" | head -n 1)
    if [ -z "$want" ]; then
        fail "$name" "缺少 '// EXIT <n>' 注释"
        continue
    fi
    if [ "$BACKEND" = c99 ]; then
        if ! $C99TCC -B "$ROOT/tcc" $TCCFLAGS \
                -o "$TMP/abi-c6.c" "$src" 2>"$TMP/err" >/dev/null; then
            fail "$name" "c6 侧生成 C 失败:$(cat "$TMP/err")"
            continue
        fi
        if [ -e "$plain" ]; then
            # 纯 C 侧：.c 文件，扩展天然不生效
            if ! $C99CC $C99FLAGS -c -o "$TMP/abi-plain.o" "$plain" \
                    2>"$TMP/err" >/dev/null; then
                fail "$name" "纯 C 侧编译失败:$(cat "$TMP/err")"
                continue
            fi
            if ! $C99CC $C99FLAGS -o "$TMP/abi.bin" \
                    "$TMP/abi-c6.c" "$TMP/abi-plain.o" 2>"$TMP/err" >/dev/null; then
                fail "$name" "链接失败:$(cat "$TMP/err")"
                continue
            fi
        else
            if ! $C99CC $C99FLAGS -o "$TMP/abi.bin" "$TMP/abi-c6.c" \
                    2>"$TMP/err" >/dev/null; then
                fail "$name" "链接失败:$(cat "$TMP/err")"
                continue
            fi
        fi
    else
        if ! $TCC $BASE $TCCFLAGS -c "$src" -o "$TMP/abi-c6.o" 2>"$TMP/err" >/dev/null; then
            fail "$name" "c6 侧编译失败:$(cat "$TMP/err")"
            continue
        fi
        if [ -e "$plain" ]; then
            # 纯 C 侧：.c 文件，扩展天然不生效
            if ! $TCC -B "$ROOT/tcc" $TCCFLAGS -c "$plain" -o "$TMP/abi-plain.o" \
                    2>"$TMP/err" >/dev/null; then
                fail "$name" "纯 C 侧编译失败:$(cat "$TMP/err")"
                continue
            fi
            if ! $TCC -B "$ROOT/tcc" $TCCFLAGS -o "$TMP/abi.bin" \
                    "$TMP/abi-c6.o" "$TMP/abi-plain.o" 2>"$TMP/err" >/dev/null; then
                fail "$name" "链接失败:$(cat "$TMP/err")"
                continue
            fi
        else
            if ! $TCC -B "$ROOT/tcc" $TCCFLAGS -o "$TMP/abi.bin" "$TMP/abi-c6.o" \
                    2>"$TMP/err" >/dev/null; then
                fail "$name" "链接失败:$(cat "$TMP/err")"
                continue
            fi
        fi
    fi
    "$TMP/abi.bin" >/dev/null 2>"$TMP/err"
    got=$?
    if [ "$got" = "$want" ]; then
        note_pass "$name: 退出码 $got"
    else
        fail "$name" "退出码 $got，期望 $want"
    fi
done

# ---- 5. C99 输出后端：生成 C 源码，系统 C 编译器编译后运行 ----
for src in "$ROOT"/tests/c99/*.c6; do
    [ -e "$src" ] || continue
    base=$(basename "$src" .c6)
    name="c99/$base"
    is_wanted "$name" || continue

    if [ ! -x "$C99TCC" ]; then
        fail "$name" "找不到 $C99TCC（先执行 make build）"
        continue
    fi
    if ! command -v "$C99CC" >/dev/null 2>&1; then
        fail "$name" "找不到系统 C 编译器 $C99CC"
        continue
    fi

    want=$(sed -n 's,^// *EXIT *\([0-9][0-9]*\)$,\1,p' "$src" | head -n 1)
    [ -n "$want" ] || want=0
    wantout=$(sed -n 's,^// *OUTPUT *\(.*\)$,\1,p' "$src" | head -n 1)

    csrc="$TMP/c99-$base.c"
    if ! $C99TCC -B "$ROOT/tcc" $TCCFLAGS -o "$csrc" "$src" \
            2>"$TMP/err" >/dev/null; then
        fail "$name" "生成 C 失败:$(cat "$TMP/err")"
        continue
    fi
    # 生成 C 这一步不许有诊断输出：合法源码上出现警告，说明前端或后端
    # 把正常代码判断错了（例如死代码里的跳转标签让"函数可能没有返回值"
    # 的检查误报），而生成的 C 之后还要过 gcc -Werror，不能带着噪声走。
    if [ -s "$TMP/err" ]; then
        fail "$name" "生成 C 时有诊断输出:$(cat "$TMP/err")"
        continue
    fi
    if ! $C99CC $C99FLAGS -o "$TMP/c99-$base.bin" "$csrc" \
            2>"$TMP/err" >/dev/null; then
        fail "$name" "$C99CC 编译生成的 C 失败:$(cat "$TMP/err")"
        continue
    fi

    out=$("$TMP/c99-$base.bin" 2>"$TMP/err")
    got=$?
    if [ "$got" != "$want" ]; then
        fail "$name" "退出码 $got，期望 $want" "$(cat "$TMP/err")"
    elif [ -n "$wantout" ] && ! printf '%s\n' "$out" | grep -qF -- "$wantout"; then
        fail "$name" "输出里没有 '$wantout'" "$out"
    else
        note_pass "$name: 退出码 $got"
    fi
done

# ---- 6. c99 后端命令行：不写 -c99，用 -o <dir> 按 TU 生成 .c ----
if [ "$BACKEND" = c99 ] && is_wanted "c99/cli"; then
    name="c99/cli"
    d="$TMP/c99-cli"
    mkdir -p "$d/out"
    cat > "$d/main.c" <<'EOF'
int part(void);
int main(void) { return part(); }
EOF
    cat > "$d/part.c" <<'EOF'
int part(void) { return 7; }
EOF
    ok=yes
    if ! $C99TCC -B "$ROOT/tcc" -o "$d/out" "$d/main.c" "$d/part.c" \
            2>"$TMP/err" >/dev/null; then
        ok=no
        detail="tcc -o <dir> 失败:$(cat "$TMP/err")"
    elif [ ! -f "$d/out/main.c" ] || [ ! -f "$d/out/part.c" ]; then
        ok=no
        detail="-o <dir> 没有为每个 TU 生成 .c"
    elif ! $C99TCC -B "$ROOT/tcc" -c -o "$d/out" "$d/main.c" "$d/part.c" \
            2>"$TMP/err" >/dev/null; then
        ok=no
        detail="tcc -c -o <dir> 失败:$(cat "$TMP/err")"
    elif ! $C99CC $C99FLAGS -o "$d/bin" "$d/out/main.c" "$d/out/part.c" \
            2>"$TMP/err" >/dev/null; then
        ok=no
        detail="$C99CC 编译生成的 C 失败:$(cat "$TMP/err")"
    else
        "$d/bin" >/dev/null 2>"$TMP/err"
        got=$?
        if [ "$got" = 7 ]; then
            note_pass "$name: 每个 TU 一个 .c，退出码 7"
        else
            ok=no
            detail="退出码 $got，期望 7"
        fi
    fi
    [ "$ok" = yes ] || fail "$name" "$detail"
fi

echo
echo "run-tests: $npass 通过, $nfail 失败"
if [ $nfail -ne 0 ]; then
    echo "run-tests: 失败:$failed_list" >&2
    exit 1
fi
exit 0
