#!/bin/sh
# 多语言异步编程基准：c6 / C(state machine) / C(pthread) / C++20 协程 /
# Go / Rust(async 手写执行器) / Rust(std 线程) / Rust(tokio) / Uya(@async_fn)。
# c6/C/C++/Go/Rust 按 BENCH_SEC 分三段各跑一次（每个指标一个干净进程，RSS
# 增量才有意义）；Uya 侧只测 create/switch（std.async 的 block_on 是探针 +
# 忙轮询模型，单任务 waker 最多挂 4 个 fd，多路并发由线程池承担），最后打表。
#
#   cmake --build build --target bench-async
#   ctest --test-dir build -R '^bench-async$' --output-on-failure
#   ./bench/async/run.sh   # 单独运行，结果写到仓库根的 build/bench-async.csv
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
# CTest 注入 C6_CASE_OUT（build/tests/bench-async）作为独立输出目录，避免
# 与其它用例抢源码树里的 build/；单独运行时退回原来的 build/。
# 统一转成绝对路径，后面 `cd bench/async` 里的 go build 也能用同一个 $out。
out=${C6_CASE_OUT:-build}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
csv=$out/bench-async.csv
: > "$csv"

echo "== 编译 =="
./tcc/c6 -B tcc -I . -o "$out/bench_c6" bench/async/bench.c6 std/os/time.c6
gcc -O2 -o "$out/bench_c" bench/async/bench_c.c -lpthread
g++ -std=c++20 -O2 -o "$out/bench_cpp" bench/async/bench_cpp.cpp
rustc --edition 2021 -O -o "$out/bench_rs" bench/async/bench_rs.rs

# tokio 是可选依赖：依赖已在本地 registry 里就 --offline 构建，否则试一次联网；
# 两样都不成就跳过这一栏（其余语言照跑）。
have_tokio=0
if command -v cargo >/dev/null 2>&1; then
    if (cd bench/async/tokio_bench \
        && { cargo build --release --offline >/dev/null 2>&1 \
             || cargo build --release >/dev/null 2>&1; }); then
        have_tokio=1
    else
        echo "  ! tokio 构建失败（离线无依赖且拉不到 crates.io），跳过 tokio 两栏" >&2
    fi
else
    echo "  ! 没有 cargo，跳过 tokio 两栏" >&2
fi
(cd bench/async && go build -o "$out/bench_go" bench_go.go)

# Uya 是可选依赖：找不到编译器就跳过（其余语言照跑）。
uya_root=${UYA_ROOT:-}
if [ -z "$uya_root" ] && command -v uya >/dev/null 2>&1; then
    uya_root=$(dirname "$(dirname "$(command -v uya)")")/lib
fi
have_uya=0
if [ -n "$uya_root" ] && [ -d "$uya_root" ] && command -v uya >/dev/null 2>&1; then
    if UYA_ROOT="$uya_root" uya build bench/async/bench.uya -o "$out/bench_uya" >/dev/null 2>&1; then
        have_uya=1
    else
        echo "  ! uya 构建失败，跳过 Uya 一栏" >&2
    fi
else
    echo "  ! 没有 uya 工具链（UYA_ROOT/命令），跳过 Uya 一栏" >&2
fi

# runner NAME CMD...：三段各跑一次，把 key,value 记进 csv
runner() {
    label=$1
    shift
    for sec in create switch conns; do
        BENCH_SEC=$sec "$@" | while IFS=, read -r key val; do
            case $key in
            lang) continue ;;
            *_error|*_note) echo "  ! $label $key=$val" >&2; continue ;;
            esac
            echo "$label,$key,$val" >> "$csv"
        done
    done
}

echo "== 跑 =="
runner "c6(async/await + epoll)"        "$out/bench_c6"
runner "C(epoll + 状态机)"              "$out/bench_c"
runner "C(pthread 一任务一线程)"        "$out/bench_c" threads
runner "C++(C++20 协程 + epoll)"        "$out/bench_cpp"
runner "Go(goroutine + netpoller)"      "$out/bench_go"
runner "Rust(async/await + 手写执行器)" "$out/bench_rs"
runner "Rust(std::thread)"              "$out/bench_rs" threads
if [ "$have_tokio" = 1 ]; then
    runner "Rust(tokio current_thread)"  ./bench/async/tokio_bench/target/release/bench_tokio
    runner "Rust(tokio multi_thread x4)" ./bench/async/tokio_bench/target/release/bench_tokio mt
fi

# Uya 一次跑完 create + switch（两段都在同一个干净进程里，秒级完成）
if [ "$have_uya" = 1 ]; then
    "$out/bench_uya" | awk -F, -v label="Uya(async_fn + await)" '
        { v[$2] = $3 }
        END {
            if (v["create_n"] + 0 > 0)
                printf "%s,create_ns_per_task,%.1f\n", label, v["create_total_ns"] / v["create_n"];
            if (v["switch_n"] + 0 > 0)
                printf "%s,switch_ns_per_round,%.1f\n", label, v["switch_total_ns"] / v["switch_n"];
            printf "%s,uya_model,probe+busy-poll\n", label;
        }' >> "$csv"
fi

# 打表：行是语言，列是三项指标
awk -F, '
{
    lang[$1] = 1
    val[$1 "," $2] = $3
    if (!($2 in seen)) { seen[$2] = 1; metric[++n] = $2 }
}
END {
    printf "%-32s %14s %14s %14s %14s\n", "语言 / 机制", "create(ns)", "switch(ns)", "conns(us)", "RSS/task(B)"
    printf "%-32s %14s %14s %14s %14s\n", "--------------------------------", "--------------", "--------------", "--------------", "--------------"
    for (l in lang) {
        c = val[l ",create_ns_per_task"]; s = val[l ",switch_ns_per_round"];
        n = val[l ",conns_us_per_conn"];  r = val[l ",rss_bytes_per_task"];
        if (c == "") c = "-"; if (s == "") s = "-";
        if (n == "") n = "-"; if (r == "") r = "-";
        printf "%-32s %14s %14s %14s %14s\n", l, c, s, n, r;
    }
}' "$csv" | sort -t' ' -k1,1
echo
echo "明细: $csv"
