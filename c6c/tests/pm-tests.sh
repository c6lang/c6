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

# 7) 固件：_start → 两阶段链接 + BIN/HEX + flash/monitor/debug 模板
mkdir -p "$WORK/fwproj"
cat > "$WORK/fwproj/start.c6" <<'EOF'
void _start(void) { }
EOF
cat > "$WORK/fwproj/c6c.yaml" <<'EOF'
name: fw
version: 0.1.0
kind: firmware
out: build/fw.elf
sources:
  - start.c6
include:
  - .
flags:
  - -nostdlib
fw_formats:
  - elf
  - bin
  - hex
flash_cmd:
  - echo
  - FLASH
  - "{elf}"
  - "{bin}"
monitor_cmd:
  - echo
  - MONITOR
debug_cmd:
  - echo
  - DEBUG
  - "{elf}"
EOF
(cd "$WORK/fwproj" && "$C6C" build > build.log 2>&1)
if [ -f "$WORK/fwproj/build/fw.elf" ] && [ -f "$WORK/fwproj/build/fw.elf.bin" ] \
   && [ -f "$WORK/fwproj/build/fw.elf.hex" ]; then
  ok "firmware: ELF + BIN + HEX"
else
  bad "firmware 产物"
fi
(cd "$WORK/fwproj" && "$C6C" flash --dry-run > flash.log 2>&1)
if grep -q 'FLASH build/fw.elf build/fw.elf.bin' "$WORK/fwproj/flash.log"; then
  ok "flash 模板展开 {elf}/{bin}"
else
  bad "flash 模板展开"
fi
(cd "$WORK/fwproj" && "$C6C" monitor > monitor.log 2>&1 && "$C6C" debug > debug.log 2>&1)
if grep -q 'MONITOR' "$WORK/fwproj/monitor.log" && grep -q 'DEBUG build/fw.elf' "$WORK/fwproj/debug.log"; then
  ok "monitor / debug 模板"
else
  bad "monitor / debug 模板"
fi

# 8) dogfood：c6c/ 自己的 c6c.yaml（真正的工程配置，不是玩具用例）
SELF=$(cd "$(dirname "$C6C")/.." && pwd)
if [ -f "$SELF/c6c.yaml" ]; then ok "c6c/ 自带 c6c.yaml"; else bad "c6c.yaml 缺失"; fi
(cd "$SELF" && "$C6C" build > /dev/null 2>&1 && "$C6C" build > "$WORK/inc.log" 2>&1)
if grep -q '已是最新' "$WORK/inc.log"; then ok "增量构建跳过重编"; else bad "增量构建"; fi
if [ -x "$SELF/build/c6c-managed" ] && "$SELF/build/c6c-managed" --version > /dev/null 2>&1; then
  ok "c6c 清单自编产物可用"
else
  bad "c6c 清单自编"
fi
(cd "$SELF" && "$C6C" package > "$WORK/pkg.log" 2>&1)
if grep -q 'c6c-0.6.2.tar.gz' "$WORK/pkg.log"; then ok "package 出 TGZ"; else bad "package 出 TGZ"; fi
(cd "$SELF" && "$C6C" install --destdir="$WORK/stage" > /dev/null 2>&1)
if [ -x "$WORK/stage/usr/bin/c6c" ] && "$WORK/stage/usr/bin/c6c" --version > /dev/null 2>&1; then
  ok "install 安装可执行 c6c"
else
  bad "install 安装"
fi
(cd "$SELF" && "$C6C" publish --dry-run > "$WORK/pub.log" 2>&1)
if grep -q '预览' "$WORK/pub.log"; then ok "publish 预览"; else bad "publish 预览"; fi

# 9) cargo/go 习惯：清单内 destdir/prefix + DESTDIR 环境变量 + 向上发现
if grep -q '^destdir: pkg$' "$WORK/app/c6c.yaml" && grep -q '^prefix: /usr$' "$WORK/app/c6c.yaml"; then
  ok "init 生成 destdir/prefix"
else
  bad "init 落点字段"
fi
rm -rf "$WORK/envstage"
(cd "$WORK/app" && DESTDIR="$WORK/envstage" PREFIX=/opt/c6 "$C6C" install > /dev/null 2>&1)
if [ -x "$WORK/envstage/opt/c6/bin/app" ]; then ok "DESTDIR/PREFIX 环境变量覆盖清单"; else bad "DESTDIR 覆盖"; fi
mkdir -p "$WORK/app/sub/deep"
(cd "$WORK/app/sub/deep" && "$C6C" env > "$WORK/env.log" 2>&1)
if grep -q "manifest=$WORK/app/c6c.yaml" "$WORK/env.log"; then
  ok "向上发现 c6c.yaml（go.mod 风格）"
else
  bad "向上发现"
fi
(cd /tmp && "$C6C" env --manifest-path="$WORK/app/c6c.yaml" > "$WORK/mp.log" 2>&1)
if grep -q 'name=app' "$WORK/mp.log"; then ok "--manifest-path"; else bad "--manifest-path"; fi

# 10) cargo 风格辅助命令：env / list --json / check / clean / profile
(cd "$WORK/app" && "$C6C" list --json > list.json 2>&1 && "$C6C" check > check.log 2>&1)
if grep -q '"sources": \["src/util.c6", "src/main.c6"\]' "$WORK/app/list.json"; then
  ok "list --json"
else
  bad "list --json"
fi
if [ $? -eq 0 ] && [ ! -s "$WORK/app/check.log" ]; then ok "check 只检查不落盘"; else bad "check"; fi
(cd "$WORK/app" && "$C6C" clean --dry-run > clean.log 2>&1)
if grep -q 'clean 预览：rm -f build/app' "$WORK/app/clean.log"; then ok "clean 预览"; else bad "clean 预览"; fi
(cd "$WORK/app" && "$C6C" env --release > rel.log 2>&1)
if grep -q 'profile=release' "$WORK/app/rel.log"; then ok "profile=release"; else bad "profile"; fi
if [ -f "$SELF/c6c.yaml" ] && grep -q '^destdir: pkg$' "$SELF/c6c.yaml"; then
  ok "c6c 自身清单含安装落点"
else
  bad "c6c 自身清单"
fi

# 11) lockfile：生成 / --locked 校验 / 漂移拒绝
(cd "$WORK/app" && "$C6C" lock > lock.log 2>&1 && "$C6C" build --locked > locked.log 2>&1)
if [ -f "$WORK/app/c6c.lock" ] && grep -q 'src/main.c6' "$WORK/app/c6c.lock"; then
  ok "lock 生成含源文件"
else
  bad "lock 生成"
fi
if grep -q '已写入 c6c.lock' "$WORK/app/lock.log"; then ok "--locked 通过"; else bad "--locked 通过"; fi
cp "$WORK/app/src/main.c6" "$WORK/app/src/main.c6.bak"
printf '\n' >> "$WORK/app/src/main.c6"
(cd "$WORK/app" && "$C6C" build --locked > drift.log 2>&1)
if [ $? -eq 2 ] && grep -q '不符' "$WORK/app/drift.log"; then ok "--locked 拒绝漂移"; else bad "--locked 漂移"; fi
mv "$WORK/app/src/main.c6.bak" "$WORK/app/src/main.c6"

# 12) doctor：源文件 / import 诊断
(cd "$WORK/app" && "$C6C" doctor > doctor.log 2>&1)
if grep -q '源文件 2（缺失 0）' "$WORK/app/doctor.log"; then ok "doctor 诊断"; else bad "doctor 诊断"; fi

# 13) 迁移：从 CMakeLists.txt / Cargo.toml 提取元数据
mkdir -p "$WORK/imcmake" "$WORK/imcargo"
cat > "$WORK/imcmake/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(hello VERSION 1.2.3 LANGUAGES C)
add_executable(hello src/main.c6)
EOF
cat > "$WORK/imcargo/Cargo.toml" <<'EOF'
[package]
name = "cargohello"
version = "2.0.1"
description = "demo from cargo"
license = "MIT"
EOF
(cd "$WORK/imcmake" && "$C6C" init --from=cmake > /dev/null 2>&1)
(cd "$WORK/imcargo" && "$C6C" init --from=cargo > /dev/null 2>&1)
if grep -q '^name: hello$' "$WORK/imcmake/c6c.yaml" && grep -q '^version: 1.2.3$' "$WORK/imcmake/c6c.yaml"; then
  ok "CMake 元数据迁移"
else
  bad "CMake 元数据迁移"
fi
if grep -q '^name: cargohello$' "$WORK/imcargo/c6c.yaml" && grep -q '^pkg_desc: demo from cargo$' "$WORK/imcargo/c6c.yaml"; then
  ok "Cargo 元数据迁移"
else
  bad "Cargo 元数据迁移"
fi

# 14) workspace：自动发现成员 + 逐成员执行
mkdir -p "$WORK/ws/a" "$WORK/ws/b"
cat > "$WORK/ws/a/c6c.yaml" <<'EOF'
name: a
version: 0.1.0
kind: app
out: build/a
sources:
EOF
cat > "$WORK/ws/b/c6c.yml" <<'EOF'
name: b
version: 0.1.0
kind: app
out: build/b
sources:
EOF
(cd "$WORK/ws" && "$C6C" workspace init > /dev/null 2>&1)
if grep -q '  - a' "$WORK/ws/c6c-workspace.yaml" && grep -q '  - b' "$WORK/ws/c6c-workspace.yaml"; then
  ok "workspace init 发现成员"
else
  bad "workspace init"
fi
(cd "$WORK/ws" && "$C6C" workspace doctor > ws.log 2>&1)
if grep -q 'workspace \[a\]' "$WORK/ws/ws.log" && grep -q 'workspace \[b\]' "$WORK/ws/ws.log"; then
  ok "workspace 逐成员执行"
else
  bad "workspace 逐成员执行"
fi
ROOT=$(cd "$SELF/.." && pwd)
(cd "$ROOT" && "$C6C" workspace list > "$WORK/root-ws.log" 2>&1)
if grep -q '^c6c$' "$WORK/root-ws.log"; then ok "仓库 workspace 配置"; else bad "仓库 workspace 配置"; fi
if grep -q 'src/pm.c6' "$SELF/c6c.lock"; then ok "c6c.lock 覆盖 pm.c6"; else bad "c6c.lock 覆盖 pm.c6"; fi

echo "c6c pm: 通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
