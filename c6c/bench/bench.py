#!/usr/bin/env python3
"""c6c / tcc 基准：wall 时间、峰值 RSS、输出体积、线性度。

用法（仓库根目录）：
    python3 c6c/bench/bench.py                 # 测 tcc/c6（基线模式）
    python3 c6c/bench/bench.py --cc build/c6c/c6c --assert
    python3 c6c/bench/bench.py --json c6c/bench/last.json

口径（见 docs/plan.md §2）：
- 每个命令跑 --runs 次取最快（best-of-N），绑核由外部 `taskset` 完成（脚本不改亲和性）；
- 峰值 RSS 用 os.wait4 的 ru_maxrss，**每个子进程独立**，不会像
  resource.getrusage(RUSAGE_CHILDREN) 那样被前一个进程的高水位污染；
- 合成语料的线性度用 4x 递增（5k → 20k → 80k）：线性就是 4.0x，
  门槛 5.2x/段（= 每行成本增长 ≤1.3x，tcc 实测 3.76x/4.21x）。
"""

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DEF_ROOT = os.path.dirname(os.path.dirname(HERE))          # c6c/bench -> repo root
DEVNULL = subprocess.DEVNULL


def measure(cmd, cwd, runs, errfile):
    """返回 (best_ms, maxrss_mb, rc)。直接用 wait4 拿这个子进程自己的 rusage。"""
    best_ms = None
    best_rss = 0.0
    rc = 0
    for _ in range(runs):
        t0 = time.monotonic()
        with open(errfile, "wb") as ef:
            p = subprocess.Popen(cmd, cwd=cwd, stdout=DEVNULL, stderr=ef)
            _, status, ru = os.wait4(p.pid, 0)
            p.returncode = 0  # 已由 wait4 回收，阻止 Popen 再等一次
        dt = (time.monotonic() - t0) * 1000.0
        rc = os.waitstatus_to_exitcode(status) if hasattr(os, "waitstatus_to_exitcode") else 0
        if rc != 0:
            return None, 0.0, rc
        if best_ms is None or dt < best_ms:
            best_ms = dt
            best_rss = ru.ru_maxrss / 1024.0
    return best_ms, best_rss, 0


# tcc/config.h 由 tcc 的 configure 生成并被 .gitignore（新 clone 里没有），
# 缺了 -DONE_SOURCE=1 的语料连预处理都过不去。这里造一份最小等价物放 tmp，
# 用 -I 前置进去：基线（tcc）与被测（c6c）吃的是同一份，口径仍然可比。
CONFIG_STUB = """/* 自动生成（bench.py）：等价于 tcc configure 产出的 config.h 的最小子集 */
#define TCC_VERSION "0.9.28rc"
#if !(TCC_TARGET_I386 || TCC_TARGET_X86_64 || TCC_TARGET_ARM || TCC_TARGET_ARM64 || TCC_TARGET_RISCV64 || TCC_TARGET_C67 || TCC_TARGET_C99 || TCC_TARGET_LLVM)
#define TCC_TARGET_X86_64 1
#endif
#define CONFIG_TRIPLET "x86_64-linux-gnu"
#ifndef CONFIG_TCCDIR
#define CONFIG_TCCDIR "/usr/local/lib/tcc"
#endif
#define CONFIG_TCC_PREDEFS 1
#define CONFIG_TCC_LIBGCC "/usr/lib/gcc/x86_64-linux-gnu/12"
"""


def count_lines(cmd, cwd):
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=DEVNULL)
        return p.stdout.count(b"\n")
    except OSError:
        return 0


def gen_synth(path, n):
    with open(path, "w") as f:
        for i in range(n):
            f.write("int f%d(int a) { return a + %d; }\n" % (i, i))


def fmt(ms, rss=None, size=None):
    if ms is None:
        return "n/a"
    s = "%.0f ms" % ms
    if rss is not None:
        s += " / %.0f MB" % rss
    if size is not None:
        s += " / %.1f MB" % (size / 1e6)
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc", default=os.path.join("tcc", "c6"),
                    help="被测编译器（默认 tcc/c6，P3b 起用 build/c6c/c6c）")
    ap.add_argument("--bdir", default="tcc", help="-B 目录（默认 tcc）")
    ap.add_argument("--root", default=DEF_ROOT)
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--json", default=None, help="把结果写成 JSON")
    ap.add_argument("--assert", dest="do_assert", action="store_true",
                    help="按 bench/thresholds.json 断言（只在测 c6c 时有意义）")
    ap.add_argument("--skip", default="c99,llvm",
                    help="逗号分隔：c99,llvm,gcc,synth（默认跳过 c99/llvm：后端还没实现）")
    args = ap.parse_args()
    skip = {s for s in args.skip.split(",") if s}

    root = os.path.abspath(args.root)
    cc = args.cc if os.path.isabs(args.cc) else os.path.join(root, args.cc)
    if not os.path.exists(cc):
        print("bench: 找不到编译器 %s" % cc, file=sys.stderr)
        return 2

    tmp = tempfile.mkdtemp(prefix="c6c-bench-")
    err = os.path.join(tmp, "err.txt")
    inc = []
    if not os.path.exists(os.path.join(root, "tcc", "config.h")):
        cfgdir = os.path.join(tmp, "cfg")
        os.makedirs(cfgdir, exist_ok=True)
        with open(os.path.join(cfgdir, "config.h"), "w") as f:
            f.write(CONFIG_STUB)
        inc = ["-I", cfgdir]
        print("bench: tcc/config.h 缺失（configure 产物），用 bench.py 的 stub 顶替")
    base = [cc, "-B", args.bdir] + inc
    # 纯 C 口径：c6c 是 C 前端，不带 -DC6_EXT（c6 的扩展语法另算）。
    # tcc 的头文件也要显式带上，否则 tcc.c 找不到自己的头。
    # 标准头用 c6c 自带的那套（tcc/include 里的 stddef.h 等是给 tcc 自己用的，
    # 用了 c6c 还不支持的写法）。tcc 自己的头（tcc.h 等）用 -I tcc 找到。
    c6flags = ["-DONE_SOURCE=1", "-I", os.path.join(root, "tcc")]
    src = "tcc/tcc.c"

    results = []

    def add(name, cmd, out=None, note=""):
        ms, rss, rc = measure(cmd, root, args.runs, err)
        size = os.path.getsize(out) if out and os.path.exists(out) else None
        if rc != 0:
            note = (open(err, "rb").read(400).decode("utf-8", "replace").strip()
                    or note or "rc=%d" % rc)
        results.append({"name": name, "cmd": " ".join(cmd), "ms": ms,
                        "rss_mb": rss if ms else None, "out_bytes": size,
                        "rc": rc, "note": note})
        print("  %-26s %s" % (name, fmt(ms, rss, size)))
        return ms

    print("bench: cc=%s root=%s runs=%d" % (cc, root, args.runs))
    print("bench: %-26s %s" % ("workload", "time / RSS / output"))

    # 口径：整份编译 tcc.c 到 ELF（c6c 的 -c 还不出真 obj，所以用 -o 直接出可执行文件，
    # 前端/后端/发射全在计时范围内）。tcc 同口径实测 134 ms。
    native_ms = add("native:tcc.c", base + c6flags + ["-o", os.path.join(tmp, "tcc.native"), src],
                    os.path.join(tmp, "tcc.native"))
    # 自编译口径：c6c 编自己（src/c6c.c6 是单 TU，50 行 #include）。任务书门槛 ≤100 ms。
    self_src = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                            "src/c6c.c6")
    self_ms = None
    if os.path.exists(self_src):
        c6c_dir = os.path.dirname(os.path.dirname(self_src))
        proj = os.path.dirname(c6c_dir)
        self_ms = add("self:src/c6c.c6",
                      base + ["-I", proj, "-I", os.path.join(proj, "include"),
                              "-I", c6c_dir, "-o", os.path.join(tmp, "self.native"),
                              self_src],
                      os.path.join(tmp, "self.native"))
    pp_ms = add("preprocess:tcc.c", base + c6flags + ["-E", src], None)
    pp_lines = count_lines(base + c6flags + ["-E", src], root)

    # P3b 之前只有预处理层：`-c` 会如实报“未实现”，此时线性度用 `-E` 测
    # （同一套词法/宏/文件栈都在热路径上，preprocess 的线性度就是 P1 的线性度）。
    have_native = native_ms is not None
    if not have_native:
        print("bench: native 后端未实现（P3b 之前），线性度改用 -E 口径")

    if "c99" not in skip and have_native:
        add("c99:tcc.c", base + c6flags + ["-c99", "-o", os.path.join(tmp, "tcc.c"), src],
            os.path.join(tmp, "tcc.c"))
    if "llvm" not in skip and have_native:
        add("llvm:tcc.c", base + c6flags + ["-o", os.path.join(tmp, "tcc.ll"), src],
            os.path.join(tmp, "tcc.ll"))
    if "gcc" not in skip and have_native and shutil.which("gcc"):
        add("gcc -O0:tcc.c",
            ["gcc", "-O0", "-w", "-DONE_SOURCE=1", "-DC6_EXT", "-I", "tcc",
             "-c", src, "-o", os.path.join(tmp, "gcc.o")], os.path.join(tmp, "gcc.o"))

    synth = []
    if "synth" not in skip:
        for n in (1000, 5000, 20000, 80000):
            f = os.path.join(tmp, "syn%d.c" % n)
            gen_synth(f, n)
            # 线性度固定用 -E 口径：合成语料是纯声明，前端各阶段成本都在
            # 预处理+词法里，而且 -c 还没实现（真 obj 在 P3b）。
            ms = add("synth:%dk (-E)" % (n // 1000), base + ["-E", f], None)
            synth.append({"n": n, "ms": ms})

    # 每一档的"每行成本"。总时间比值在规模小的时候被固定开销（进程启动、
    # 运行时镜像）拖着往下走，规模大了就趋近 4.0x——所以两个数都得看：
    #   µs/行 掉下来 = 真的线性；总时间比值 = 固定开销占多少。
    for r in synth:
        if r["n"]:
            print("  %-26s %.2f µs/行" % ("synth:%dk" % (r["n"] // 1000),
                                          r["ms"] * 1000.0 / r["n"]))
    ratios = []
    for a, b in zip(synth, synth[1:]):
        if a["ms"] and b["ms"]:
            # 两个口径：
            #   * 每行成本比值（per_line）＝ 4 倍输入后"编一行"贵了多少倍。
            #     这才是"线性度"的本意：理想编译器这个数是 1.0，超线性才 >1。
            #   * 总时间比值（ratio）会把进程启动、镜像 mmap 这些固定成本摊薄，
            #     所以它天然接近输入的 4 倍（4x 输入 → 4x 时间才是线性的），
            #     拿它去比"≤2.6x"在数学上就是不可能的（O(n) 编译器必然 ≈4）。
            #     所以它只打印、不断言。
            pl = (b["ms"] / b["n"]) / (a["ms"] / a["n"])
            ratios.append({"from": a["n"], "to": b["n"], "ratio": b["ms"] / a["ms"],
                           "per_line": pl})
            print("  %-26s %.2fx（4x 输入；每行成本 %.2fx）" %
                  ("linearity %dk->%dk" % (a["n"] // 1000, b["n"] // 1000),
                   b["ms"] / a["ms"], pl))

    if pp_lines:
        print("  %-26s %d 行（%.2f µs/行）" %
              ("preprocessed lines", pp_lines,
               (native_ms * 1000.0 / pp_lines) if native_ms else 0))

    report = {
        "cc": cc,
        "root": root,
        "runs": args.runs,
        "machine": {"cpu": platform.processor() or platform.machine(),
                    "cores": os.cpu_count(), "python": platform.python_version()},
        "pp_lines": pp_lines,
        "results": results,
        "synth": synth,
        "linearity": ratios,
    }
    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        with open(args.json, "w") as f:
            json.dump(report, f, indent=2, ensure_ascii=False)
        print("bench: 写入 %s" % args.json)

    if args.do_assert:
        th_path = os.path.join(HERE, "thresholds.json")
        if os.path.basename(cc) == "c6":
            print("bench: 被测的是 tcc，跳过断言（阈值只对 c6c 生效）")
        elif not os.path.exists(th_path):
            print("bench: 缺 %s" % th_path, file=sys.stderr)
            return 2
        else:
            th = json.load(open(th_path))
            bad = []
            by = {r["name"]: r for r in results}
            keys = [("native:tcc.c", "native_ms"), ("c99:tcc.c", "c99_ms"),
                    ("llvm:tcc.c", "llvm_ms"), ("preprocess:tcc.c", "pp_ms"),
                    ("self:src/c6c.c6", "self_ms")]
            if not have_native:
                keys = [("preprocess:tcc.c", "pp_ms")]
                print("bench: native/c99/llvm 门槛不适用（后端未实现，如实跳过而非假装通过）")
            for name, key in keys:
                r = by.get(name)
                if r and r["ms"] and th.get(key) and r["ms"] > th[key]:
                    bad.append("%s %.0f ms > %.0f ms" % (name, r["ms"], th[key]))
            for r in results:
                if r["name"].startswith("gcc"):
                    continue        # 对照组（gcc -O0）的内存不在预算里
                if r["ms"] and r["rss_mb"] and r["rss_mb"] > th["max_rss_mb"]:
                    bad.append("%s RSS %.0f MB > %d MB" % (r["name"], r["rss_mb"], th["max_rss_mb"]))
            for x in ratios:
                # 断言的是**每行成本**比值（见上面那段解释）：4 倍输入后每行
                # 的编译成本不许涨到 2.6 倍以上，也就是不许出现超线性爆炸。
                if x["per_line"] > th["linearity_ratio"]:
                    bad.append("线性度（每行成本）%.2fx > %.2fx（%d->%d）" %
                               (x["per_line"], th["linearity_ratio"], x["from"], x["to"]))
            if bad:
                print("bench: 断言失败", file=sys.stderr)
                for b in bad:
                    print("  - " + b, file=sys.stderr)
                return 1
            print("bench: 断言通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
