#!/bin/sh
# -g（调试信息）的回归：-c 出的 .o 里带 DWARF 行号表 + 函数 DIE。
#
# 口径：
#   * 结构：.debug_abbrev / .debug_info / .debug_line / .debug_str 四个段在，
#     两张重定位表（.rela.debug_info / .rela.debug_line）把地址指回 .text
#     —— .o 里的地址是段内偏移，必须靠重定位让链接器补成最终地址；
#   * 内容：行号表里能读到源码的每一行，DIE 里有 main / add 两个函数；
#   * 真用起来：用系统 cc 链成可执行文件后，addr2line 认得出 `dbg.c6:10`，
#     gdb（装了才测）的 `info line main` / `break main` 也认；
#   * 不加 -g 时一个 .debug_* 段都不该有（默认行为不变）。
set -u

C6C=${C6C:-./build/c6c}
CC=${CC:-cc}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
TMPDIR="$WORK/tmp"
export TMPDIR
mkdir -p "$TMPDIR"

pass=0
fail=0
ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1${2:+：$2}"; }
have() { command -v "$1" >/dev/null 2>&1; }

case "$C6C" in
/*) ;;
*) C6C="$(pwd)/$C6C" ;;
esac

cat >"$WORK/dbg.c6" <<'EOF'
int printf(const char *fmt, ...);

static int add(int a, int b)
{
    return a + b;
}

int main(void)
{
    int x = 2;
    int y = 3;
    int z = add(x, y);

    printf("z=%d\n", z);
    return z == 5 ? 0 : 1;
}
EOF

if ! "$C6C" -g -c "$WORK/dbg.c6" -o "$WORK/dbg.o" >"$WORK/c.log" 2>&1; then
    bad "-g -c 编译" "$(tail -3 "$WORK/c.log")"
    echo "c6c -g 回归：通过 $pass / 失败 $fail"
    exit 1
fi
ok "-g -c 编译通过（$("$C6C" --version 2>/dev/null | head -1 | cut -c1-20)…）"

if ! have readelf; then
    echo "c6c -g 回归：跳过结构核对（没有 readelf）"
    echo "c6c -g 回归：通过 $pass / 失败 $fail"
    [ "$fail" -eq 0 ]
    exit $?
fi

secs=$(readelf -SW "$WORK/dbg.o" 2>/dev/null)
miss=""
for s in .debug_abbrev .debug_info .debug_line .debug_str .rela.debug_info .rela.debug_line; do
    printf '%s\n' "$secs" | grep -q " $s " || miss="$miss $s"
done
if [ -z "$miss" ]; then
    ok "四个 DWARF 段 + 两张重定位表都在"
else
    bad "DWARF 段齐全" "缺：$miss"
fi

# 重定位都指回 .text（R_X86_64_64）
rel=$(readelf -rW "$WORK/dbg.o" 2>/dev/null)
ninfo=$(printf '%s\n' "$rel" | grep "R_X86_64_64" | wc -l)
if [ "$ninfo" -ge 3 ]; then
    ok "地址字段带 R_X86_64_64 重定位（$ninfo 条：CU + 每个函数）"
else
    bad "地址重定位" "只找到 $ninfo 条"
fi

line=$(readelf --debug-dump=decodedline "$WORK/dbg.o" 2>/dev/null)
okline=1
for n in 5 10 11 12 14 15; do
    printf '%s\n' "$line" | grep -qE " +$n +0x" || okline=0
done
if [ "$okline" = 1 ]; then
    ok "行号表覆盖源码的每一行（5 / 10 / 11 / 12 / 14 / 15）"
else
    bad "行号表内容" "$(printf '%s\n' "$line" | head -6 | tr '\n' ' ')"
fi

info=$(readelf --debug-dump=info "$WORK/dbg.o" 2>/dev/null)
if printf '%s\n' "$info" | grep -q "DW_TAG_compile_unit" &&
        printf '%s\n' "$info" | grep -q ": add" &&
        printf '%s\n' "$info" | grep -q ": main"; then
    ok "DIE 里是编译单元 + add / main 两个函数"
else
    bad "DIE 内容" "$(printf '%s\n' "$info" | head -4 | tr '\n' ' ')"
fi

# --- 链接成可执行文件后用起来：addr2line ---
if "$CC" -g -o "$WORK/dbgprog" "$WORK/dbg.o" >"$WORK/cc.log" 2>&1; then
    out=$("$WORK/dbgprog")
    if [ "$out" = "z=5" ]; then
        ok "系统 cc 链得上、跑得出（z=5）"
    else
        bad "链接后的程序" "输出『$out』"
    fi
    if have addr2line && have nm; then
        maddr=$(nm "$WORK/dbgprog" 2>/dev/null | awk '$3=="main"{print $1}')
        got=$(addr2line -e "$WORK/dbgprog" -f "0x$maddr" 2>/dev/null | tr '\n' ' ')
        case "$got" in
        *"dbg.c6:10"*) ok "addr2line：main 的地址落到 dbg.c6:10（带编译时的目录前缀）" ;;
        *) bad "addr2line 结果" "『$got』（应含 dbg.c6:10）" ;;
        esac
    fi
    if have gdb; then
        g=$(gdb -batch -ex "info line main" -ex "break main" "$WORK/dbgprog" 2>&1)
        if printf '%s\n' "$g" | grep -q 'Line 10 of "dbg.c6"' &&
                printf '%s\n' "$g" | grep -q "file dbg.c6, line 10"; then
            ok "gdb：info line main / break main 都认得出 dbg.c6:10"
        else
            bad "gdb 认行号" "$(printf '%s\n' "$g" | grep -E 'Line|Breakpoint' | head -2 | tr '\n' ' ')"
        fi
    else
        echo "skip gdb（没装）"
    fi
else
    bad "系统 cc 链接 .o" "$(tail -3 "$WORK/cc.log")"
fi

# --- 不加 -g：一个 .debug_* 段都不该有 ---
"$C6C" -c "$WORK/dbg.c6" -o "$WORK/nog.o" >/dev/null 2>&1
n=$(readelf -SW "$WORK/nog.o" 2>/dev/null | grep -c "\.debug")
if [ "$n" = 0 ]; then
    ok "不加 -g 时没有 .debug_* 段（默认行为不变）"
else
    bad "不加 -g 的产物" "出现了 $n 个 .debug_* 段"
fi

echo "c6c -g 回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
