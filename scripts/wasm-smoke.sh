#!/bin/sh
# wasm 冒烟：tcc --target=<triple> 生成 .ll 交给 clang，再用 node 跑一遍。
#
#   ./scripts/wasm-smoke.sh [triple]
#
# 默认 triple 是 wasm32-unknown-unknown：wasm64 的 long 和指针与 IR 的 LP64
# 模型天然一致，但 LLVM 19 的 wasm-ld 给函数指针表发出的 elem 段偏移用的是
# 内存地址（64 位），模块一实例化就 "table index is out of bounds"（见
# docs/llvm-backend.md）。所以冒烟跑 wasm32：它的 ABI 适配由
# tcc/lib/wasm-rt.c 负责。
#
# 没有 clang 或 node 时只跑“生成 IR”那一段，并说明跳过了什么。
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TRIPLE=${1:-wasm32-unknown-unknown}
ARCH=${TRIPLE%%-*}
TCC=$ROOT/tcc/tcc
RT=$ROOT/tcc/libtcc1-$ARCH.a
NODE=${NODE:-node}
CC=${LLVMCC:-clang}
command -v "$CC" >/dev/null 2>&1 || CC=/usr/lib/llvm-19/bin/$CC

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0

say() { printf '%s\n' "$*"; }

[ -x "$TCC" ] || { say "wasm-smoke: 先跑 make build"; exit 1; }

cat > "$TMP/prog.c6" <<'EOF'
/* 纯计算的入口：不碰 libc */
int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
EOF

cat > "$TMP/gc.c6" <<'EOF'
/* 碰运行库：管理对象（引用计数 GC）和切片 */
struct Node { int v; };

static @Node mk(int n) { @Node p = new Node; p->v = n; return p; }

int pair_sum(void)
{
    @Node a = mk(20);
    @Node b = mk(22);
    return a->v + b->v;
}

int sum(int[:] s)
{
    int i = 0, total = 0;
    for (i = 0; i < (int)s.len; i++)
        total += s[i];
    return total;
}

/* 大结构体按值传参：入口的形参拷贝走的是 llvm.memcpy intrinsic */
struct Big { int a[8]; };

int big_sum(struct Big b)
{
    int i = 0, t = 0;
    for (i = 0; i < 8; i++)
        t += b.a[i];
    return t;
}

int big(void)
{
    struct Big b = { { 0, 0, 0, 0, 0, 0, 0, 0 } };
    int i = 0;
    for (i = 0; i < 8; i++)
        b.a[i] = i;
    return big_sum(b);                /* 0+1+...+7 */
}

int slice_sum(void)
{
    int a[4] = { 1, 2, 3, 4 };
    int[:] s = a;
    int[:] t = s[1:3];
    return sum(t) + (int)s.len;      /* (2+3) + 4 */
}
EOF

say "wasm-smoke: triple=$TRIPLE"

# 1) 只要 IR：triple 和 datalayout 必须跟着目标走
"$TCC" -B "$ROOT/tcc" --target=$TRIPLE --emit-llvm -o "$TMP/prog.ll" "$TMP/prog.c6"
if grep -q "target triple = \"$TRIPLE\"" "$TMP/prog.ll"; then
    say "  ok   --emit-llvm 写出 $TRIPLE 的模块头"
else
    say "  FAIL --emit-llvm 的模块头不是 $TRIPLE"; fail=1
fi

# 2) 目标运行库
if [ ! -f "$RT" ]; then
    say "  ..   没有 $RT，先构建"
    LLVMCC=$CC "$ROOT/scripts/build-wasm-runtime.sh" $TRIPLE >/dev/null
fi

# 3) 直接出 .wasm（demo 是仓库自带的例子：printf + usleep + spawn + async，
#    走的是运行库的控制台和内联线程）
"$TCC" -B "$ROOT/tcc" --target=$TRIPLE -o "$TMP/prog.wasm" "$TMP/prog.c6"
"$TCC" -B "$ROOT/tcc" --target=$TRIPLE -o "$TMP/gc.wasm" "$TMP/gc.c6"
"$TCC" -B "$ROOT/tcc" --target=$TRIPLE -o "$TMP/demo.wasm" \
    "$ROOT/examples/gc_demo.c6"
for f in prog gc demo; do
    [ -s "$TMP/$f.wasm" ] || { say "  FAIL $f.wasm 没生成"; fail=1; }
done

# 4) 跑起来
if command -v "$NODE" >/dev/null 2>&1; then
    "$NODE" -e '
const fs = require("fs");
const dir = process.argv[1];
let bad = 0;
/* --no-entry 的模块要自己跑构造器：数据节里的指针由 @__llvm_init 补齐 */
function inst(name) {
    const buf = fs.readFileSync(dir + "/" + name + ".wasm");
    if (!WebAssembly.validate(buf)) { console.log("  FAIL " + name + ".wasm 校验失败"); bad++; return null; }
    const ex = new WebAssembly.Instance(new WebAssembly.Module(buf)).exports;
    if (ex.__wasm_call_ctors) ex.__wasm_call_ctors();
    return ex;
}
function run(name, checks) {
    const ex = inst(name);
    if (!ex) return;
    for (const [call, want] of checks) {
        const [fn, args] = call.split(":");
        const argv = args ? args.split(",").map(Number) : [];
        const got = ex[fn](...argv);
        const ok = Number(got) === want;
        console.log((ok ? "  ok   " : "  FAIL ") + fn + "(" + argv + ") = " + got + "，期望 " + want);
        if (!ok) bad++;
    }
}
/* 控制台：printf 写进运行库的缓冲区，main() 的返回值是失败项数 */
function demo(name, want) {
    const ex = inst(name);
    if (!ex) return;
    const rc = Number(ex.main());
    const ptr = Number(ex.__c6_wasm_con_ptr());
    const len = Number(ex.__c6_wasm_con_len());
    const text = new TextDecoder().decode(new Uint8Array(ex.memory.buffer, ptr, len));
    const last = text.trim().split("\n").pop();
    const ok = rc === 0 && text.includes(want);
    console.log((ok ? "  ok   " : "  FAIL ") + "gc_demo: main() = " + rc +
                "，最后一行 " + JSON.stringify(last));
    if (!ok) { console.log(text); bad++; }
}
run("prog", [["add:2,3", 5], ["mul:4,5", 20]]);
run("gc", [["pair_sum", 42], ["slice_sum", 9], ["big", 28]]);
demo("demo", "全部检查通过");
process.exit(bad ? 1 : 0);
' "$TMP" || fail=1
else
    say "  ..   没有 node，跳过运行（.wasm 已生成）"
fi

if [ $fail -ne 0 ]; then
    say "wasm-smoke: 失败"
    exit 1
fi
say "wasm-smoke: 通过"
