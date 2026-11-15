#!/bin/sh
# 目标（2）（3）的总记分板：一次跑完三把尺子，各一行。
#
#   ① tcc tests2，c6c 直接编（这就是"c6c 直接跑通 tcc 上游用例"那条验收）
#   ② tcc tests/ 下的老式单文件用例（tests1 口径）
#   ③ tcc tests2，由 **c6c 编出来的 tcc** 跑（G2c：自举链闭上的证据）
#
# 用法：tests/scoreboard.sh [-j N]        （G2C=0 可跳过 ③）
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${ROOT:-$HERE/../..}
C6C=${C6C:-$HERE/../build/c6c}
export C6C ROOT
JOBS=1
case "${1:-}" in
-j*) JOBS=${1#-j} ;;
esac
printf '=== c6c 对 tcc 上游用例的记分板 ===\n'
"$HERE/tcc-tests2.sh" -j "$JOBS" 2>/dev/null | grep '^tcc tests2'
"$HERE/tcc-tests1.sh" 2>/dev/null | grep '^tcc tests1'
if [ "${G2C:-1}" = 1 ] && [ -x "$HERE/../build/tcc-c6c" ]; then
    ROOT=$ROOT TCC=$HERE/../build/tcc-c6c "$HERE/g2c-tests2.sh" 2>/dev/null \
        | grep -E '^g2c|^c6c-built' | head -2
else
    echo "g2c：（没有 build/tcc-c6c，先 make build-tcc）"
fi
