#!/bin/sh
# 前端/后端回归：每个 .c 用 c6c 编成可执行再跑，退出码与 gcc 编同一份的结果比对。
# 没有 gcc 时退回到文件头注释里的 `/* expect: N */`。
set -u
CC=${C6C:-build/c6c}
DIR=${1:-tests/c}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0
skip=0
for src in "$DIR"/*.c; do
    base=$(basename "$src" .c)
    want=$(sed -n 's|/\* *expect: *\([0-9]*\).*|\1|p' "$src" | head -1)
    # 可选的每例参数：`/* args: -nostdlib */`。给 c6c 和 gcc 都用同一份，
    # 这样两边比的是同一件事（内核口径的用例要 -nostdlib/-ffreestanding-kernel）。
    args=$(sed -n 's|/\* *args: *\(.*\) *\*/.*|\1|p' "$src" | head -1)
    # 仅编译（`/* compile-only */`）：不进链接、不跑。内核口径的用例常常
    # 没有 c6c 的运行时（-nostdlib），链接不起来，但"能不能编过"才是要钉的。
    compile_only=0
    grep -q 'compile-only' "$src" && compile_only=1
    if [ "$compile_only" = "1" ]; then
        if command -v gcc >/dev/null 2>&1; then
            if ! gcc -std=c99 -w $args -fsyntax-only "$src" >/dev/null 2>&1; then
                echo "SKIP $base（gcc 也不接受，oracle 无效）"
                skip=$((skip + 1))
                continue
            fi
        fi
        if "$CC" $args -fsyntax-only "$src" >"$TMP/out_$base" 2>"$TMP/err_$base"; then
            pass=$((pass + 1))
        else
            echo "FAIL $base（编译失败）"
            sed -n '1,3p' "$TMP/err_$base" | sed 's/^/      /'
            fail=$((fail + 1))
        fi
        continue
    fi
    if command -v gcc >/dev/null 2>&1; then
        if gcc -std=c99 -w $args -o "$TMP/g_$base" "$src" 2>/dev/null; then
            "$TMP/g_$base"
            want=$?
        fi
    fi
    # 用 `-o` 直接出可执行（native 后端本来就只出整份 ELF；`-c` 是 P7-5 之前
    # 的遗留，它不走"构造函数表"那条路，`__thread` 的 %fs 基址就没人设）
    if ! "$CC" $args "$src" -o "$TMP/c_$base" >"$TMP/out_$base" 2>"$TMP/err_$base"; then
        echo "FAIL $base（编译失败）"
        sed -n '1,3p' "$TMP/err_$base" | sed 's/^/      /'
        fail=$((fail + 1))
        continue
    fi
    "$TMP/c_$base"
    got=$?
    if [ "$got" = "$want" ]; then
        pass=$((pass + 1))
    else
        echo "FAIL $base：期望 $want，实际 $got"
        fail=$((fail + 1))
    fi
done
echo "前端回归：通过 $pass / 失败 $fail / 跳过 $skip（gcc 作为 oracle）"
[ "$fail" -eq 0 ]
