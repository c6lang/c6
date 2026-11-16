#!/bin/sh
# 动态链接的回归：`c6c -Wl,--dynamic -o prog a.o rt.o` 出能跑的动态可执行文件。
#
# 验的是"顶替 cc"最实的那一段：程序里调 libc（getpid / strlen / getcwd……）时，
# 不用再 -Wl,--allow-undefined 把未定义符号按 0 处理，而是让 ld.so 从
# libc.so.6 解析——真的调进去、真的拿到值。
#
# 口径：
#   * 结构：ET_EXEC + 两个 PT_LOAD（各自页对齐）+ PT_INTERP + PT_DYNAMIC，
#     DT_NEEDED 是 libc.so.6，JUMP_SLOT 条数等于导入个数；
#   * 行为：libc 函数返回的值要写进程序输出（pid 与 getppid 的关系、
#     strlen 的长度），退出码是 main 的返回值；
#   * 默认仍是静态链接：不加 -Wl,--dynamic 时同一个 .o 会因为未定义符号报错
#     （说明"动态"是显式选项，不是偷偷打开）；
#   * 数据符号（environ 那种）这一版不做 COPY 重定位：要打一条点名警告，
#     但程序照样能跑。
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

have() { command -v "$1" >/dev/null 2>&1; }

# readelf 的输出是**按 locale 翻译**的：中文 locale 下程序头表头是「程序头：」、
# 首行是「Elf 文件类型为 EXEC (Executable file)」，C locale 下是 "Program Headers:"
# / "Elf file type is EXEC (Executable file)"。开发机是 zh_CN、CI 的 runner 是
# C，同一份产物会看到两种文字——所以凡是解析它的地方都走这个固定 C locale 的
# 包装，两边拿到同一串英文，断言才不会跟着机器语言变。
readelf() { LC_ALL=C command readelf "$@"; }
case "$C6C" in
/*) ;;
*) C6C="$(pwd)/$C6C" ;;
esac
HERE=$(cd "$(dirname "$0")/.." && pwd)
RT="$HERE/lib/rt.c6"

# --- 构造被测程序：调几个 libc 函数，把返回值写进输出 ---
cat >"$WORK/libcuser.c6" <<'EOF'
int printf(const char *fmt, ...);
int getpid(void);
int getppid(void);
long strlen(const char *s);
char *getcwd(char *buf, unsigned long size);

int main(void)
{
    char buf[512] = {0};
    char *cwd = getcwd(buf, sizeof(buf));
    int pid = getpid();
    int ppid = getppid();

    printf("dyn pid=%d ppid_ok=%d len=%ld cwd=%s\n",
           pid, (ppid > 0), strlen("dynamic"), cwd ? cwd : "(null)");
    return pid > 0 ? 7 : 1;
}
EOF

"$C6C" -c "$WORK/libcuser.c6" -o "$WORK/libcuser.o" >"$WORK/c1.log" 2>&1 || {
    bad "编译 libcuser.o" "$(tail -3 "$WORK/c1.log")"
    echo "c6c 动态链接回归：通过 $pass / 失败 $fail"
    exit 1
}
"$C6C" -c "$RT" -o "$WORK/rt.o" >"$WORK/c2.log" 2>&1 || {
    bad "编译 rt.o" "$(tail -3 "$WORK/c2.log")"
    echo "c6c 动态链接回归：通过 $pass / 失败 $fail"
    exit 1
}

# --- 1. 动态链接本身：不加 --allow-undefined 也要链接成功 ---
if "$C6C" -Wl,--dynamic -o "$WORK/prog" "$WORK/libcuser.o" "$WORK/rt.o" \
        >"$WORK/link.log" 2>"$WORK/link.err"; then
    ok "动态链接成功（没有用 -Wl,--allow-undefined）"
else
    bad "动态链接成功" "$(tail -3 "$WORK/link.log" "$WORK/link.err" 2>/dev/null | tr '\n' ' ')"
fi

if grep -q "动态链接 libc.so.6" "$WORK/link.log"; then
    ok "链接器自己说清楚了走的是动态链接（并报出导入个数）"
else
    bad "链接器报出动态链接" "$(cat "$WORK/link.log")"
fi

if grep -q "1 个数据 COPY\|数据 COPY" "$WORK/link.log"; then
    ok "数据符号走 COPY 重定位（链接器把个数报出来）"
else
    bad "数据符号 COPY" "$(cat "$WORK/link.log")"
fi

# --- 2. 真跑：libc 函数返回的值要出现在输出里 ---
got=$("$WORK/prog" 2>"$WORK/run.err")
rc=$?
if [ "$rc" = 7 ]; then
    ok "动态产物真跑起来，退出码是 main 的返回值（7）"
else
    bad "动态产物退出码" "退出 $rc（应为 7），输出『$got』"
fi
case "$got" in
"dyn pid="*" ppid_ok=1 len=7 cwd=/"*)
    ok "libc 函数真的被调用：getpid/getppid/strlen/getcwd 的返回值都对" ;;
*)
    bad "libc 函数返回值" "输出『$got』" ;;
esac

# --- 3. 结构：readelf 逐项核对 ---
if have readelf; then
    ph=$(readelf -lW "$WORK/prog" 2>/dev/null)
    dyn=$(readelf -dW "$WORK/prog" 2>/dev/null)
    # 产物没有段表，重定位要从动态段读：--use-dynamic
    rel=$(readelf -rW --use-dynamic "$WORK/prog" 2>/dev/null)

    type=$(printf '%s\n' "$ph" | sed -n 's/^Elf file type is \([A-Z_]*\).*/\1/p')
    if [ "${type:-}" = "EXEC" ]; then
        ok "产物是 ET_EXEC"
    else
        bad "产物是 ET_EXEC" "类型『${type:-?}』"
    fi
    nload=$(printf '%s\n' "$ph" | grep -c "^  LOAD")
    if [ "$nload" = 2 ]; then
        ok "两个 PT_LOAD（代码 R+E / 数据 R+W）"
    else
        bad "两个 PT_LOAD" "实际 $nload 个"
    fi
    if printf '%s\n' "$ph" | grep -q "Requesting program interpreter: /lib64/ld-linux-x86-64.so.2"; then
        ok "PT_INTERP 指向 ld-linux-x86-64.so.2"
    else
        bad "PT_INTERP" "没写解释器路径"
    fi
    if printf '%s\n' "$ph" | grep -q "DYNAMIC"; then
        ok "PT_DYNAMIC 存在"
    else
        bad "PT_DYNAMIC" "没有动态段"
    fi
    if printf '%s\n' "$dyn" | grep -q "(NEEDED).*libc.so.6"; then
        ok "DT_NEEDED = libc.so.6"
    else
        bad "DT_NEEDED" "$(printf '%s\n' "$dyn" | head -3 | tr '\n' ' ')"
    fi
    if printf '%s\n' "$dyn" | grep -q "BIND_NOW"; then
        ok "DT_FLAGS 置了 BIND_NOW（非惰性绑定）"
    else
        bad "BIND_NOW" "没有置位"
    fi
    njs=$(printf '%s\n' "$rel" | grep -c "JUMP_SLOT")
    nimp=$(sed -n 's/.*导入 \([0-9]*\) 个函数.*/\1/p' "$WORK/link.log")
    if [ -n "${nimp:-}" ] && [ "$njs" = "$nimp" ]; then
        ok "JUMP_SLOT 条数（$njs）等于导入的函数个数"
    else
        bad "JUMP_SLOT 条数" "重定位 $njs 条，链接器报导入 ${nimp:-?} 个"
    fi
else
    echo "skip readelf 结构核对（没装 readelf）"
fi

# --- 3b. COPY 重定位：libc 里的数据符号真被拷进来（optind 静态初值是 1） ---
cat >"$WORK/copy.c6" <<'EOF'
int printf(const char *fmt, ...);
extern int optind;

int main(void)
{
    printf("optind=%d\n", optind);
    return optind == 1 ? 7 : 1;
}
EOF
if "$C6C" -c "$WORK/copy.c6" -o "$WORK/copy.o" >/dev/null 2>&1 &&
        "$C6C" -Wl,--dynamic -o "$WORK/copyp" "$WORK/copy.o" "$WORK/rt.o" \
            >"$WORK/copy.log" 2>&1; then
    got=$("$WORK/copyp")
    rc=$?
    if [ "$rc" = 7 ] && [ "$got" = "optind=1" ]; then
        ok "COPY 重定位：optind 从 libc 拷进来（值 1、大小 4 字节）"
    else
        bad "COPY 重定位的值" "退出码 $rc，输出『$got』"
    fi
    if have readelf; then
        sym=$(readelf -sW --use-dynamic "$WORK/copyp" 2>/dev/null)
        if printf '%s\n' "$sym" | grep -q " 8 OBJECT  GLOBAL DEFAULT  ABS environ"; then
            ok "environ 在 .dynsym 里是 8 字节的 OBJECT（大小取自 libc 的符号表）"
        else
            bad "environ 的 dynsym 项" "$(printf '%s\n' "$sym" | grep environ | head -1)"
        fi
        dyn=$(readelf -dW "$WORK/copyp" 2>/dev/null)
        if printf '%s\n' "$dyn" | grep -q "(RELA)" && printf '%s\n' "$dyn" | grep -q "(RELASZ)"; then
            ok "DT_RELA / DT_RELASZ 存在（COPY 走 .rela.dyn）"
        else
            bad "DT_RELA" "$(printf '%s\n' "$dyn" | grep -E 'RELA' | tr '\n' ' ')"
        fi
        ncopy=$(readelf -rW --use-dynamic "$WORK/copyp" 2>/dev/null | grep -c COPY)
        if [ "$ncopy" -ge 1 ]; then
            ok "R_X86_64_COPY 重定位条目在（$ncopy 条）"
        else
            bad "COPY 重定位条目" "没找到"
        fi
    fi
else
    bad "COPY 用例链接" "$(tail -3 "$WORK/copy.log" 2>/dev/null)"
fi

# --- 4. 默认还是静态：不加 -Wl,--dynamic 时未定义符号照样报错 ---
if "$C6C" -o "$WORK/prog-static" "$WORK/libcuser.o" "$WORK/rt.o" \
        >"$WORK/static.log" 2>"$WORK/static.err"; then
    bad "默认静态链接会对未定义符号报错" "居然链接成功了"
else
    if grep -q "未定义的符号" "$WORK/static.err"; then
        ok "默认静态链接：未定义符号如实报错（动态是显式选项）"
    else
        bad "默认静态链接的报错内容" "$(tail -2 "$WORK/static.err")"
    fi
fi

# --- 5. ldd 认得出来（装得下 ld.so 的常识检验） ---
if have ldd; then
    if ldd "$WORK/prog" 2>/dev/null | grep -q "libc.so.6"; then
        ok "ldd 认出 libc.so.6 依赖"
    else
        bad "ldd 认出 libc.so.6" "$(ldd "$WORK/prog" 2>&1 | head -2 | tr '\n' ' ')"
    fi
else
    echo "skip ldd（没装）"
fi

echo "c6c 动态链接回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
