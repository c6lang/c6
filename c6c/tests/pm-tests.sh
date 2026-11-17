#!/bin/sh
# c6c 工程管理子命令的回归：init/scan/build/test/run + 清单解析 + 原 CLI 不回归。
set -u

C6C=${C6C:-./build/c6c}
case "$C6C" in
  /*) ;;
  *) C6C=$(cd "$(dirname "$C6C")" && pwd)/$(basename "$C6C") ;;
esac
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0
ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1${2:+：$2}"; }

mkdir -p "$WORK/app/src"
cat > "$WORK/app/src/main.c6" <<'EOF'
int util_answer(void);
int main(void) { return util_answer() == 42 ? 0 : 1; }
test "answer" {
    if (util_answer() != 42)
        return err(1, "answer != 42");
}
EOF
cat > "$WORK/app/src/util.c6" <<'EOF'
int util_answer(void) { return 42; }
EOF

# 1) scan 不改文件、能推断 app
(cd "$WORK/app" && "$C6C" scan > scan.log 2>&1)
if grep -q 'kind=app' "$WORK/app/scan.log" && grep -q '源文件 2 个' "$WORK/app/scan.log"; then
  ok "scan 推断 app + 两个源文件"
else
  bad "scan 推断"
fi
if [ -e "$WORK/app/c6c.yaml" ]; then bad "scan 不该写清单"; else ok "scan 只读"; fi

# 2) init 写清单
(cd "$WORK/app" && "$C6C" init > init.log 2>&1)
if [ -f "$WORK/app/c6c.yaml" ] && grep -q '^kind: app$' "$WORK/app/c6c.yaml" \
   && grep -q '  - src/main.c6' "$WORK/app/c6c.yaml"; then
  ok "init 生成 c6c.yaml"
else
  bad "init 生成清单"
fi

# 3) build / run / test 闭环
(cd "$WORK/app" && "$C6C" build > build.log 2>&1)
if [ -x "$WORK/app/build/app" ]; then ok "build 产出 build/app"; else bad "build 产出"; fi
"$WORK/app/build/app"
if [ $? -eq 0 ]; then ok "产物退出码 0"; else bad "产物退出码"; fi
(cd "$WORK/app" && "$C6C" test > test.log 2>&1)
if grep -q '1 test(s), 0 failure(s)' "$WORK/app/test.log"; then ok "test 跑 test 块"; else bad "test 跑 test 块"; fi
(cd "$WORK/app" && "$C6C" run > run.log 2>&1)
if [ $? -eq 0 ]; then ok "run 先编再跑"; else bad "run 先编再跑"; fi

# 4) init 覆盖保护 / --force
(cd "$WORK/app" && "$C6C" init > again.log 2>&1)
if [ $? -eq 2 ]; then ok "init 默认拒绝覆盖"; else bad "init 覆盖保护"; fi
(cd "$WORK/app" && "$C6C" init --force > force.log 2>&1)
if [ $? -eq 0 ]; then ok "init --force 覆盖"; else bad "init --force"; fi

# 5) 固件推断：_start 无 main → firmware + -nostdlib
mkdir -p "$WORK/fw"
cat > "$WORK/fw/start.c6" <<'EOF'
void _start(void) { for (;;) {} }
EOF
(cd "$WORK/fw" && "$C6C" init > init.log 2>&1)
if grep -q '^kind: firmware$' "$WORK/fw/c6c.yaml" && grep -q -- '-nostdlib' "$WORK/fw/c6c.yaml"; then
  ok "init 推断 firmware + -nostdlib"
else
  bad "firmware 推断"
fi

# 6) 原编译器 CLI 不回归（子命令名不能吃掉普通编译）
"$C6C" --version > /dev/null 2>&1
if [ $? -eq 0 ]; then ok "--version 正常"; else bad "--version"; fi
printf 'int main(void){return 7;}\n' > "$WORK/hello.c6"
"$C6C" -o "$WORK/hello" "$WORK/hello.c6" > /dev/null 2>&1
"$WORK/hello"
if [ $? -eq 7 ]; then ok "普通编译仍是编译器"; else bad "普通编译"; fi

echo "c6c pm: 通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
