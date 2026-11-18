#!/bin/bash
# c6c-host —— kbuild 的 HOSTCC/HOSTLD 包装器。
#
# kbuild 的 host-cmulti（多 TU 链接）不走 c6c 单 TU 那条"自动带 rt.c6"的
# 路，直接链 a.o b.o 会缺 malloc/printf。本包装器给链接模式置
# C6C_LINK_WITH_RT=1，让 c6c 在 M_LINK 时临时编 rt.c6 追加进链接输入。
# 编译模式（-c）不受影响。
#
# 用法：make HOSTCC=/path/to/c6c-host ...
set -u
C6C=${C6C:-/media/winger/_dde_data/superc/c6c/build/c6c}
export C6C_LINK_WITH_RT=1
exec "$C6C" "$@"
