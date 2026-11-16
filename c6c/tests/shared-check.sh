#!/bin/sh
# 共享库（`c6c -shared a.o -o liba.so`）的回归：ET_DYN 产物到底能不能用。
#
# 验的是"c6c 能出 .so"最实的那几段：
#   * 结构：ET_DYN、入口 0、没有 PT_INTERP（它不自己起进程）、有 PT_DYNAMIC、
#     SONAME 就是输出文件名、节头表在（GNU ld 把 .so 当输入时只认节头，
#     没它就 "file in wrong format"）；
#   * 导出：定义的全局符号进 .dynsym（函数 T、数据 D，段号别一律写 .text）；
#   * 重定位：代码本身位置无关，数据里的绝对地址（字符串指针那种）进
#     .rela.dyn 当 R_X86_64_RELATIVE，ld.so 加载时统一加基址；
#   * 行为①：dlopen + dlsym 拿到函数指针，调用真的跑通，读到的数据符号
#     是库里的那份（写一下再读）；
#   * 行为②：`__attribute__((constructor))` 在 dlopen 时就跑了
#     （DT_INIT_ARRAY），不用谁来手动调；
#   * 行为③：别的程序用 cc 直接链这个 .so 也能跑（SONAME + 节头都对才行）；
#   * 边界：`-shared` 的输入必须是 .o（给源文件要报错）、共享库里的外部
#     **数据**符号（optind 那种）要如实拒绝而不是悄悄写 0；
#   * `-fPIC` 收下不报错（native 后端本来就位置无关），不加 `-shared` 时
#     同一个 .o 仍然出 ET_EXEC（说明它是显式开关，不是偷偷打开）。
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

# readelf/nm 的输出会按 locale 翻译，统一走 C locale，断言才不跟机器语言变
readelf() { LC_ALL=C command readelf "$@"; }
nm() { LC_ALL=C command nm "$@"; }

case "$C6C" in
/*) ;;
*) C6C="$(pwd)/$C6C" ;;
esac

# --- 被测库：函数 + 数据 + 构造器 + 一个字符串指针（要 RELATIVE） ---
cat >"$WORK/lib.c6" <<'EOF'
#include <stdio.h>

int counter = 7;
const char *greeting = "hello from .so";
int ready = 0;

__attribute__((constructor)) static void boot(void)
{
    ready = 42;
}

int add(int a, int b) { return a + b; }
int bump(void) { return ++counter; }
int booted(void) { return ready; }
void say(void) { printf("%s (counter=%d)\n", greeting, counter); }
EOF

if ! "$C6C" -c "$WORK/lib.c6" -o "$WORK/lib.o" >"$WORK/c1.log" 2>&1; then
    bad "编译 lib.c6" "$(tail -3 "$WORK/c1.log")"
    echo "c6c 共享库回归：通过 $pass / 失败 $fail"
    exit 1
fi
if ! "$C6C" -shared "$WORK/lib.o" -o "$WORK/libshare.so" >"$WORK/link.log" 2>&1; then
    bad "链接 libshare.so" "$(tail -3 "$WORK/link.log")"
    echo "c6c 共享库回归：通过 $pass / 失败 $fail"
    exit 1
fi
ok "-shared 出共享库（$(sed -n 's/.*（共享库 \(.*\)）/\1/p' "$WORK/link.log" | head -1)）"

# --- 1. ELF 结构 ---
if have readelf; then
    hdr=$(readelf -hW "$WORK/libshare.so" 2>/dev/null)
    if printf '%s\n' "$hdr" | grep -q "Type:.*DYN"; then
        ok "e_type 是 ET_DYN"
    else
        bad "e_type" "$(printf '%s\n' "$hdr" | grep -i "Type" | head -1)"
    fi
    if printf '%s\n' "$hdr" | grep -q "Entry point address:.*0x0$"; then
        ok "入口是 0（共享库没有 _start）"
    else
        bad "入口" "$(printf '%s\n' "$hdr" | grep -i "Entry" | head -1)"
    fi

    ph=$(readelf -lW "$WORK/libshare.so" 2>/dev/null)
    if printf '%s\n' "$ph" | grep -q "INTERP"; then
        bad "PT_INTERP" "共享库不该有解释器段"
    else
        ok "没有 PT_INTERP（不自己起进程）"
    fi
    if printf '%s\n' "$ph" | grep -q "DYNAMIC"; then
        ok "有 PT_DYNAMIC"
    else
        bad "PT_DYNAMIC" "程序头里没有"
    fi
    np=$(printf '%s\n' "$ph" | sed -n 's/^  Number of program headers: *//p' | head -1)
    case "${np:-}" in
    ""|*[!0-9]*) np=$(printf '%s\n' "$hdr" | sed -n 's/^ *Number of program headers: *\([0-9]*\)/\1/p' | head -1) ;;
    esac
    if [ "${np:-0}" = 4 ]; then
        ok "程序头 4 个（两个 LOAD + DYNAMIC + GNU_STACK）"
    else
        bad "程序头个数" "是 ${np:-?}，应为 4"
    fi

    dyn=$(readelf -dW "$WORK/libshare.so" 2>/dev/null)
    if printf '%s\n' "$dyn" | grep -q "SONAME.*\[libshare.so\]"; then
        ok "DT_SONAME 是 libshare.so（别的产物按这个名字记依赖）"
    else
        bad "DT_SONAME" "$(printf '%s\n' "$dyn" | grep -i soname | head -1)"
    fi
    if printf '%s\n' "$dyn" | grep -q "(INIT_ARRAY)"; then
        ok "DT_INIT_ARRAY 在（构造器交给 ld.so 调）"
    else
        bad "DT_INIT_ARRAY" "$(printf '%s\n' "$dyn" | grep -E "INIT" | tr '\n' ' ')"
    fi

    # 数据里的绝对地址 → RELATIVE
    if readelf -rW --use-dynamic "$WORK/libshare.so" 2>/dev/null | grep -q "R_X86_64_RELATIVE"; then
        ok "字符串指针走 R_X86_64_RELATIVE（ld.so 加基址）"
    else
        bad "R_X86_64_RELATIVE" "$(readelf -rW --use-dynamic "$WORK/libshare.so" 2>&1 | head -3 | tr '\n' ' ')"
    fi

    # 节头表：GNU ld 把 .so 当输入时只认它
    secs=$(readelf -SW "$WORK/libshare.so" 2>/dev/null)
    missing=""
    for want in .text .dynsym .dynstr .hash .rela.dyn .dynamic .shstrtab; do
        printf '%s\n' "$secs" | grep -q " $want " || missing="$missing $want"
    done
    if [ -z "$missing" ]; then
        ok "节头表齐全（.dynsym / .dynstr / .hash / .rela.dyn / .dynamic / .shstrtab）"
    else
        bad "节头表" "缺：$missing"
    fi
else
    echo "skip readelf 结构核对（没装 readelf）"
fi

# --- 2. 导出的符号：函数 T、数据 D ---
if have nm; then
    syms=$(nm -D --defined-only "$WORK/libshare.so" 2>/dev/null)
    if printf '%s\n' "$syms" | grep -q " T add$" &&
            printf '%s\n' "$syms" | grep -q " T booted$"; then
        ok "函数导出成 T（add / booted）"
    else
        bad "函数导出" "$(printf '%s\n' "$syms" | tr '\n' ' ')"
    fi
    if printf '%s\n' "$syms" | grep -q " D counter$" &&
            printf '%s\n' "$syms" | grep -q " D greeting$"; then
        ok "数据导出成 D（counter / greeting）"
    else
        bad "数据导出" "$(printf '%s\n' "$syms" | grep -E "counter|greeting" | tr '\n' ' ')"
    fi
else
    echo "skip nm 导出符号核对（没装 nm）"
fi

# --- 3. dlopen + dlsym：真的调进去 ---
cat >"$WORK/driver.c" <<'EOF'
#include <stdio.h>
#include <dlfcn.h>

int main(int argc, char **argv)
{
    void *h = dlopen(argv[1], RTLD_NOW);
    int (*add)(int, int) = NULL;
    int (*bump)(void) = NULL;
    int (*booted)(void) = NULL;
    void (*say)(void) = NULL;
    int *counter = NULL;

    if (!h) {
        printf("dlopen 失败: %s\n", dlerror());
        return 1;
    }
    add = dlsym(h, "add");
    bump = dlsym(h, "bump");
    booted = dlsym(h, "booted");
    say = dlsym(h, "say");
    counter = dlsym(h, "counter");
    if (!add || !bump || !booted || !say || !counter) {
        printf("dlsym 失败\n");
        return 2;
    }
    /* 构造器在 dlopen 那一刻就该跑过了 */
    if (booted() != 42) {
        printf("构造器没跑：ready=%d\n", booted());
        return 3;
    }
    printf("add=%d ", add(20, 22));
    printf("counter=%d ", *counter);
    *counter = 100;
    printf("bump=%d ", bump());
    say();
    return 0;
}
EOF

if ! "$CC" -o "$WORK/driver" "$WORK/driver.c" -ldl >"$WORK/drv.log" 2>&1; then
    bad "编译 dlopen 驱动（cc -ldl）" "$(tail -3 "$WORK/drv.log")"
    echo "c6c 共享库回归：通过 $pass / 失败 $fail"
    exit 1
fi
got=$("$WORK/driver" "$WORK/libshare.so" 2>&1)
rc=$?
if [ "$rc" = 0 ] && printf '%s\n' "$got" | grep -q "add=42 counter=7 bump=101"; then
    ok "dlopen + dlsym：函数调通、数据符号读写一致（$got）"
else
    bad "dlopen + dlsym" "退出码 $rc，输出『$got』"
fi
if printf '%s\n' "$got" | grep -q "hello from .so (counter="; then
    ok "字符串指针经 RELATIVE 后指向正确的字符串"
else
    bad "RELATIVE 后的字符串" "输出『$got』"
fi

# --- 4. 别的程序用 cc 直接链这个 .so ---
cat >"$WORK/use.c" <<'EOF'
#include <stdio.h>

int add(int a, int b);
int booted(void);
int counter;

int main(void)
{
    counter = 5;
    printf("direct add=%d booted=%d\n", add(1, 2), booted());
    return add(1, 2) == 3 && booted() == 42 ? 0 : 1;
}
EOF

if "$CC" -o "$WORK/usep" "$WORK/use.c" -L"$WORK" -lshare >"$WORK/use.log" 2>&1; then
    got=$(LD_LIBRARY_PATH="$WORK" "$WORK/usep" 2>&1)
    rc=$?
    if [ "$rc" = 0 ] && [ "$got" = "direct add=3 booted=42" ]; then
        ok "cc 直接链 .so（-L … -lshare）跑通：$got"
    else
        bad "直接链接后运行" "退出码 $rc，输出『$got』"
    fi
else
    bad "cc 直接链 .so" "$(tail -3 "$WORK/use.log")"
fi

# --- 5. 边界：-shared 要 .o 输入；外部数据符号如实拒绝 ---
if "$C6C" -shared "$WORK/lib.c6" -o "$WORK/x.so" >"$WORK/e1.log" 2>&1; then
    bad "-shared 给源文件" "居然成功了"
else
    if grep -q "输入得是 .o" "$WORK/e1.log"; then
        ok "-shared 给源文件时如实报错（先 -c 再 -shared）"
    else
        bad "-shared 给源文件的错误信息" "$(tail -2 "$WORK/e1.log" | tr '\n' ' ')"
    fi
fi

cat >"$WORK/extdata.c6" <<'EOF'
extern int optind;

int peek(void) { return optind; }
EOF
if "$C6C" -c "$WORK/extdata.c6" -o "$WORK/extdata.o" >/dev/null 2>&1 &&
        "$C6C" -shared "$WORK/extdata.o" -o "$WORK/libext.so" >"$WORK/e2.log" 2>&1; then
    bad "共享库里的外部数据符号" "居然链接成功了（会到运行期才崩）"
else
    if grep -q "不支持外部数据符号" "$WORK/e2.log"; then
        ok "共享库里的外部数据符号如实拒绝（要走 GOT，还没做）"
    else
        bad "外部数据符号的错误信息" "$(tail -2 "$WORK/e2.log" 2>/dev/null | tr '\n' ' ')"
    fi
fi

# --- 6. -fPIC 收下；不加 -shared 时仍是 ET_EXEC（显式开关） ---
cat >"$WORK/hello.c6" <<'EOF'
int main(void) { return 7; }
EOF
if "$C6C" -c -fPIC "$WORK/hello.c6" -o "$WORK/pic.o" >"$WORK/e3.log" 2>&1 &&
        "$C6C" -fPIC -o "$WORK/exe" "$WORK/pic.o" >>"$WORK/e3.log" 2>&1; then
    ok "-fPIC 收下不报错（native 后端本来就位置无关）"
else
    bad "-fPIC" "$(tail -3 "$WORK/e3.log")"
fi
if have readelf; then
    if readelf -hW "$WORK/exe" 2>/dev/null | grep -q "Type:.*EXEC"; then
        ok "不加 -shared 时仍是 ET_EXEC（共享库是显式选项）"
    else
        bad "默认产物类型" "$(readelf -hW "$WORK/exe" 2>&1 | grep -i type | head -1)"
    fi
fi

echo "c6c 共享库回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
