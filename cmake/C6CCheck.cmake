# c6c 的验收 target。
#
# 全部走 c6c 自己的脚本（tests/*.sh、scripts/lint.sh、bench/bench.py），区别只在
# "谁来发起"：以前是 make，现在是 CMake。脚本的口径没变，所以数字可比。
#
# 由 cmake/C6C.cmake 顺序 include；聚合门禁（fast / check-all-c6c）与 CTest
# 注册在 C6CGate.cmake，安装与库元数据在 C6CInstall.cmake。

# c6c_target(<名字> [NO_C6C_DEP] [DEPENDS t...] [ENV k=v ...] COMMAND a b c ...)
#
# 给出一个 target：跑给定命令，工作目录固定在 c6c/，默认依赖编好的 c6c。
function(c6c_target name)
  cmake_parse_arguments(ARG "NO_C6C_DEP" "COMMENT" "COMMAND;ENV;DEPENDS" ${ARGN})
  set(_deps ${ARG_DEPENDS})
  if(NOT ARG_NO_C6C_DEP)
    list(APPEND _deps c6c)
  endif()
  if(NOT ARG_COMMENT)
    set(ARG_COMMENT "c6c: ${name}")
  endif()
  add_custom_target(${name}
    COMMAND ${CMAKE_COMMAND} -E env ${_c6c_ld_env} ${ARG_ENV} ${ARG_COMMAND}
    WORKING_DIRECTORY "${C6C_DIR}"
    DEPENDS ${_deps}
    USES_TERMINAL
    COMMENT "${ARG_COMMENT}"
    VERBATIM)
endfunction()

# --- 2.1 内存内数据结构自测 / IR 稳定性 / ELF 骨架 ---
c6c_target(unit
  COMMAND "${C6C_BIN}" --type-selftest
  COMMAND "${C6C_BIN}" --ir-selftest
  COMMENT "c6c: unit（类型表 + IR 自测）")

c6c_target(irtest
  ENV C6C=${C6C_BIN} OUT=${C6C_BUILD}
  COMMAND "${C6C_BIN}" --ir-selftest
  COMMAND ./tests/ir-demo-stable.sh
  COMMENT "c6c: irtest（IR 自测 + dump 稳定）")

c6c_target(hello
  COMMAND "${C6C_BIN}" --hello "${C6C_BUILD}/hello"
  COMMAND sh -c "\"${C6C_BUILD}/hello\"; rc=$?; if [ $rc -ne 42 ]; then echo \"c6c: hello 退出码 $rc（应为 42）\"; exit 1; fi; echo \"c6c: hello 退出码 42（IR -> x86-64 -> ELF -> 内核装载，不经 tcc/gcc/libc）\""
  COMMENT "c6c: hello（自己发的 ELF 能被内核装载）")

# --- 2.2 前端 / 后端 / 命令行回归 ---
c6c_target(runflag ENV C6C=${C6C_BIN} COMMAND ./tests/run-flag.sh)
c6c_target(testflag ENV C6C=${C6C_BIN} COMMAND ./tests/test-flag.sh)
c6c_target(ctest-c6c ENV C6C=${C6C_BIN} COMMAND ./tests/run.sh
  COMMENT "c6c: ctest-c6c（前端/后端回归，tests/run.sh）")

# --- 2.2b 工程管理子命令（扫描 / 清单 / 编译 / 测试 / 打包安装 / 固件 / lock / workspace / 迁移）---
c6c_target(pmtest ENV C6C=${C6C_BIN} COMMAND ./tests/pm-tests.sh
  COMMENT "c6c: pmtest（c6c 工程管理子命令全链路回归，56 项检查）")

# --- 2.3 各后端 / 各特性的专项回归 ---
c6c_target(objcheck ENV C6C=${C6C_BIN} COMMAND ./tests/obj-check.sh)
c6c_target(ldcheck ENV C6C=${C6C_BIN} COMMAND ./tests/ld-check.sh)
c6c_target(dyncheck ENV C6C=${C6C_BIN} COMMAND ./tests/dyn-check.sh)
c6c_target(sharedcheck ENV C6C=${C6C_BIN} COMMAND ./tests/shared-check.sh)
# printf 的口径（宽度/精度/标志/%x/%g/舍入/-0.0）：同一份矩阵用宿主 cc 与 c6c
# 各跑一遍逐行 diff，c6c/lib/rt.c6 的 c6c_vfmt 一旦和 glibc 分叉就在这里露出来。
c6c_target(printfcheck ENV C6C=${C6C_BIN} COMMAND ./tests/printf-check.sh)
# 二进制转义（`"\xde"` 是**一个字节**，不是 UTF-8 的 0xc3 0x9e）：窄字面量里
# 只有 `\u` 和源码里的 UTF-8 才该重新编码，这条线曾经把 std 的 5 个用例带偏。
c6c_target(esccheck ENV C6C=${C6C_BIN} COMMAND ./tests/esc-check.sh)
# 嵌套模板实例化：A@T 的方法里用到 B@T 时，B 的方法体要真发出来（否则链接期
# 报 `<实例名>_<方法>` 未定义）。std 的 `Vect@T.iter()` 返回 `Iter@T` 就是这个形状。
c6c_target(tmplcheck ENV C6C=${C6C_BIN} COMMAND ./tests/tmpl-check.sh)
# 用 c6c 跑根目录 tests/regression.sh 里已经全绿的那批用例（列表单一数据源：
# c6c/tests/regression-c6c.list）。交付物要能不带 tcc，这批回归就得能只靠 c6c 跑。
c6c_target(regression-c6c ENV C6C=${C6C_BIN} COMMAND ./tests/regression-c6c.sh)
c6c_target(bcheck ENV C6C=${C6C_BIN} COMMAND ./tests/bcheck.sh)
c6c_target(dbgcheck ENV C6C=${C6C_BIN} COMMAND ./tests/dbgcheck.sh)
c6c_target(asmcheck ENV C6C=${C6C_BIN} COMMAND ./tests/asmcheck.sh)
c6c_target(a64check ENV C6C=${C6C_BIN} COMMAND ./tests/a64check.sh)
c6c_target(archcheck ENV C6C=${C6C_BIN} COMMAND ./tests/archcheck.sh)

c6c_target(embedcheck DEPENDS c6c-lib
  ENV C6C_LIB=${C6C_BUILD} C6C_INC=${C6C_DIR}/include
  COMMAND ./tests/embed-check.sh)

c6c_target(pkgcheck DEPENDS c6c-lib
  ENV CC=${CMAKE_C_COMPILER}
  COMMAND ./tests/pkgcheck.sh)

# --- 2.4 隔离检查（目录边界 / 许可证 / 必需文件）---
c6c_target(lint NO_C6C_DEP COMMAND ./scripts/lint.sh)

# --- 2.5 自举：fixpoint 与"产出的编译器也能自测" ---
c6c_target(selfhost
  ENV C6C=${C6C_BIN} BUILD=${C6C_BUILD} SRC=${C6C_DIR}/src/c6c.c6
      C6C_RT=${C6C_DIR}/lib/rt.c6 INC=${C6C_DIR}/include
  COMMAND ./tests/selfhost.sh
  COMMENT "c6c: selfhost（c6c 编自己两遍，逐字节 fixpoint）")

add_custom_target(selftest
  COMMAND "${C6C_BIN}" -o "${C6C_BUILD}/c6c-self" "${C6C_DIR}/src/c6c.c6"
  COMMAND "${C6C_BUILD}/c6c-self" --type-selftest
  COMMAND "${C6C_BUILD}/c6c-self" --ir-selftest
  DEPENDS c6c
  USES_TERMINAL
  COMMENT "c6c: selftest（自举产出的编译器通过自带自测）")

# --- 2.6 语料与记分板 ---
c6c_target(stdtest ENV C6C=${C6C_BIN} ROOT=${C6C_ROOT}
  COMMAND ./tests/std-tests.sh -j ${C6C_JOBS})

c6c_target(test-c6c ENV C6C=${C6C_BIN} ROOT=${C6C_ROOT}
  COMMAND ./tests/run-tests.sh -j ${C6C_JOBS}
  COMMENT "c6c: test-c6c（tests/ 下的 c6 用例：前端能过 + test 块能跑）")

c6c_target(diagtest ENV C6C=${C6C_BIN} ROOT=${C6C_ROOT}
  COMMAND ./tests/diag-tests.sh -j ${C6C_JOBS}
  COMMENT "c6c: diagtest（tests/errors 里的编译期诊断）")

c6c_target(c99test ENV C6C=${C6C_BIN} CC=${CMAKE_C_COMPILER}
  CORPUS=${C6C_DIR}/tests/c
  COMMAND ./tests/c99-diff.sh
  COMMENT "c6c: c99test（native vs C99 后端差分）")

c6c_target(llvmtest ENV C6C=${C6C_BIN} LLC=${C6C_LLC} CC=${CMAKE_C_COMPILER}
  CORPUS=${C6C_DIR}/tests/c
  COMMAND ./tests/llvm-diff.sh
  COMMENT "c6c: llvmtest（native vs LLVM IR 后端差分，LLC=${C6C_LLC}）")

c6c_target(c99suite ENV C6C=${C6C_BIN} ROOT=${C6C_ROOT}
  COMMAND ./tests/c99-suite.sh -j ${C6C_JOBS}
  COMMENT "c6c: c99suite（third_party/c-testsuite 150 条，语言层闸门）")

c6c_target(tests1-check ENV C6C=${C6C_BIN} ROOT=${C6C_ROOT}
  COMMAND ./tests/tcc-tests1.sh
  COMMENT "c6c: tests1-check（tcc 上游 tests/ 老式单文件用例）")

c6c_target(tests2-check ENV C6C=${C6C_BIN} ROOT=${C6C_ROOT}
  COMMAND ./tests/tcc-tests2.sh -j ${C6C_JOBS}
  COMMENT "c6c: tests2-check（tcc 上游 tests2，tests/tcc-na.txt 之外不许失败）")

c6c_target(tcc-tests ENV C6C=${C6C_BIN}
  COMMAND ./tests/tcc-tests2.sh -j ${C6C_JOBS})

c6c_target(scoreboard ENV C6C=${C6C_BIN} ROOT=${C6C_ROOT}
  COMMAND ./tests/scoreboard.sh -j ${C6C_JOBS})

c6c_target(scan ENV C6C=${C6C_BIN} ROOT=${C6C_ROOT}
  COMMAND sh -c "ok=0; bad=0; for f in \$(ls \"${C6C_ROOT}/tcc\"/*.c | grep -v il-gen.c) \$(find \"${C6C_ROOT}/std\" -name '*.c6' | sort); do if \"${C6C_BIN}\" --scan \"\$f\" > /dev/null 2>&1; then ok=\$((ok + 1)); else bad=\$((bad + 1)); echo \"扫描失败：\$f\"; fi; done; echo \"c6c: 预处理+词法 通过 \$ok / 失败 \$bad\"; [ \$bad -eq 0 ]"
  COMMENT "c6c: scan（用 c6c 的词法扫 tcc/*.c 与 std/*.c6）")

# --- 2.7 性能（验收 (4)，门槛在 bench/thresholds.json）---
c6c_target(bench ENV C6C=${C6C_STAGE0} ROOT=${C6C_ROOT}
  COMMAND "${Python3_EXECUTABLE}" "${C6C_ROOT}/c6c/bench/bench.py" --root ${C6C_ROOT} --cc ${C6C_STAGE0}
          --runs 5 --json "${CMAKE_BINARY_DIR}/c6c-bench-last.json"
  NO_C6C_DEP
  COMMENT "c6c: bench（用 tcc/c6 跑基线；BOOTSTRAP=host 时不适用）")

c6c_target(bench-c6c DEPENDS c6c-opt ENV ROOT=${C6C_ROOT}
  COMMAND "${Python3_EXECUTABLE}" "${C6C_DIR}/bench/bench.py" --root ${C6C_ROOT} --cc "${C6C_OPT}"
          --runs 5 --assert --json "${CMAKE_BINARY_DIR}/c6c-bench-last.json"
  COMMENT "c6c: bench-c6c（c6c-opt 跑分并按 thresholds.json 断言）")

# --- 2.8 c6c 编出上游 tcc（G2c），再让那个 tcc 跑 tests2 ---
# 这一步编的是 tcc 的**源码**（语料），它要用三个**生成**物：
#   config.h / config.mak —— tcc/CMakeLists.txt 生成（tcc-config target）
#   tccdefs_.h            —— tcc/Makefile 的规则（宿主 cc 出 c2str 再转换）
#   libtcc1.a             —— tcc 的 C 运行库，必须由**能用的 tcc** 编
# 三个都不需要产品构建的 tcc（一个靠 cmake，另两个靠宿主 cc），所以 tcc 没建
# 也能把语料备齐。已经由 tcc 的 CMake 构建备好时这三步都是空操作。
c6c_target(c6c-tcc-corpus NO_C6C_DEP DEPENDS c6c-opt
  ENV ROOT=${C6C_ROOT} BUILD=${C6C_BUILD} CMAKE=${CMAKE_COMMAND}
      MAKE=${C6_MAKE_EXECUTABLE}
  COMMAND ./tests/tcc-corpus.sh
  COMMENT "c6c: 备齐 tcc 的语料依赖（config.h / tccdefs_.h / libtcc1.a）")

add_custom_target(build-tcc
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${C6C_BUILD}"
  COMMAND "${C6C_OPT}" -o "${C6C_BUILD}/tcc-c6c" -DONE_SOURCE=1
          "-I${C6C_ROOT}/tcc" "-I${C6C_DIR}/include" "${C6C_ROOT}/tcc/tcc.c"
  COMMAND ${CMAKE_COMMAND} -E env ${_c6c_ld_env}
          "C6C_TCC_SELF_MIN=$ENV{C6C_TCC_SELF_MIN}"
          "${C6C_DIR}/tests/tcc-self-check.sh" "${C6C_BUILD}/tcc-c6c"
  DEPENDS c6c-tcc-corpus
  WORKING_DIRECTORY "${C6C_DIR}"
  USES_TERMINAL
  COMMENT "c6c: build-tcc（c6c 编出上游 tcc，再由它跑 tests2 + 第三代 selfhost）")

c6c_target(g2c-check DEPENDS build-tcc ENV ROOT=${C6C_ROOT}
  COMMAND ./tests/g2c-tests2.sh
  COMMENT "c6c: g2c-check（c6c 编出的 tcc 跑它自己的 tests2）")
