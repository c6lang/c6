# c6c —— 用 c6 语言写的自举编译器（native x86-64 / aarch64 / C99 / LLVM IR 后端）
#
# **c6c 没有自己的构建系统了**：编 c6c、跑它的全部验收，都是这份文件定义的
# CMake target。`c6c/Makefile` 已经删掉，`make -C c6c ...` 那条路不再存在。
#
# 常用入口（`-S . -B build` 之后）：
#
#   cmake --build build                       默认目标：c6 产品 + c6c + libc6c
#   cmake --build build --target c6c          只编 c6c（`c6c/build/c6c`）
#   cmake --build build --target c6c-lib      只编嵌入库 libc6c.a / .so
#   cmake --build build --target fast         c6c 快速门禁（≈40 s，日常改一行）
#   cmake --build build --target check-all-c6c  全量验收（≈4 min，提交/发布前）
#   cmake --build build --target help         CMake 自己会列出全部 target
#   ctest -L c6c                              CTest 视角的同一套验收
#
# 每个 CMake target 对应的就是原来 c6c/Makefile 里的同名目标（逐条映射见
# `c6c/docs/acceptance.md` 的表格；只有两个改了名字：`test` → `test-c6c`、
# `ctest` → `ctest-c6c`，因为 `test` 是 CMake 内建 target）。
#
# 引导编译器（stage0）：
#   C6_ENABLE_TCC=ON（默认）→ 仓库自己的 tcc/c6，`-B tcc` 带 crt / libtcc1.a；
#   C6_ENABLE_TCC=OFF        → 宿主 C 编译器，`-x c` 直接编 c6c 的单 TU 源码。
# 两条路产物同一份源码、同一套语义；OFF 这条路**不需要任何 tcc 构建产物**。

set(C6C_DIR "${CMAKE_CURRENT_SOURCE_DIR}/c6c")
set(C6C_BUILD "${C6C_DIR}/build")
set(C6C_BIN "${C6C_BUILD}/c6c")
set(C6C_OPT "${C6C_BUILD}/c6c-opt")
set(C6C_LIB_A "${C6C_BUILD}/libc6c.a")
set(C6C_LIB_SO "${C6C_BUILD}/libc6c.so")
set(C6C_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# bench.py 是 Python 脚本：显式用解释器调用，不依赖文件可执行位
# （git 里它是 100644，AUR check 直接 exec 会得到 permission denied）。
# Python3 是 c6c 跑分/回归的依赖，详见 README 与 docs/manual/testing.md。
find_package(Python3 COMPONENTS Interpreter REQUIRED)

include(ProcessorCount)
ProcessorCount(C6C_JOBS)
if(C6C_JOBS EQUAL 0)
  set(C6C_JOBS 4)
endif()

# 引导编译器 tcc/c6 在 shared 形态（-DC6_TCC_SHARED_LIBTCC=ON）下自己就链到
# libtcc.so：构建树里的 c6 没有 rpath，直接执行会报 libtcc.so 找不到。所以凡是
# 要**执行** c6 的地方都补 LD_LIBRARY_PATH，口径与 C6Testing.cmake 一致。
set(_c6c_ld_env "")
if(C6_ENABLE_TCC AND NOT C6_TCC_STATIC)
  set(_c6c_ld_path "${C6_TCC_DIR}:$ENV{LD_LIBRARY_PATH}")
  string(REGEX REPLACE ":+$" "" _c6c_ld_path "${_c6c_ld_path}")
  set(_c6c_ld_env "LD_LIBRARY_PATH=${_c6c_ld_path}")
endif()

file(GLOB C6C_SRCS CONFIGURE_DEPENDS "${C6C_DIR}/src/*.c6")

# llc 常常装在 /usr/lib/llvm-NN/bin 而不是 PATH 里：先 PATH，再挑一个装好的版本
find_program(C6C_LLC NAMES llc)
if(NOT C6C_LLC)
  file(GLOB _c6c_llcs "/usr/lib/llvm-*/bin/llc")
  if(_c6c_llcs)
    list(SORT _c6c_llcs COMPARE NATURAL)
    list(GET _c6c_llcs -1 C6C_LLC)
  endif()
endif()

# ---------------------------------------------------------------------------
# 1. 编 c6c（单 TU 引导）与嵌入库
# ---------------------------------------------------------------------------
if(C6_ENABLE_TCC)
  set(C6C_STAGE0 "${C6_TCC_DIR}/c6${C6_TCC_EXESUF}")
  add_custom_command(
    OUTPUT "${C6C_BIN}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${C6C_BUILD}"
    COMMAND ${CMAKE_COMMAND} -E env ${_c6c_ld_env}
            "${C6C_STAGE0}" -B "${C6_TCC_DIR}" -g -o "${C6C_BIN}"
            "${C6C_DIR}/src/c6c.c6"
    DEPENDS ${C6C_SRCS} "${C6C_STAGE0}"
    COMMENT "c6c: 用 ${C6C_STAGE0} 编出自举编译器 (c6c/build/c6c)"
    VERBATIM)
else()
  set(C6C_STAGE0 "${CMAKE_C_COMPILER}")
  add_custom_command(
    OUTPUT "${C6C_BIN}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${C6C_BUILD}"
    COMMAND "${CMAKE_C_COMPILER}" -x c -O2 -o "${C6C_BIN}" "${C6C_DIR}/src/c6c.c6"
    DEPENDS ${C6C_SRCS}
    COMMENT "c6c: 用 ${CMAKE_C_COMPILER} 编出自举编译器 (c6c/build/c6c，stage0 = 宿主 CC)"
    VERBATIM)
endif()

add_custom_target(c6c ALL DEPENDS "${C6C_BIN}")
if(C6_ENABLE_TCC)
  add_dependencies(c6c tcc-build)
else()
  # 不建 tcc，但 tcc 的**配置文件**还是要生成：build-tcc 把 tcc/*.c 当语料编，
  # 那份源码 `#include "config.h"`。tcc-config 只同步配置头，不碰编译产物。
  add_dependencies(c6c tcc-config)
  add_dependencies(c6 c6c)
  add_dependencies(build c6c)
endif()
set_target_properties(c6c PROPERTIES ADDITIONAL_CLEAN_FILES "${C6C_BIN}")

# 性能口径的测量二进制：gcc -O3（tcc 的代码生成比 gcc 差 4 倍，拿 tcc 编的
# 二进制量性能等于在量 tcc 的寄存器分配）。
add_custom_command(
  OUTPUT "${C6C_OPT}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${C6C_BUILD}"
  COMMAND "${CMAKE_C_COMPILER}" -x c -O3 -o "${C6C_OPT}" "${C6C_DIR}/src/c6c.c6"
  DEPENDS ${C6C_SRCS}
  COMMENT "c6c: 编测量二进制 c6c/build/c6c-opt（${CMAKE_C_COMPILER} -O3）"
  VERBATIM)
add_custom_target(c6c-opt DEPENDS "${C6C_OPT}")
set_target_properties(c6c-opt PROPERTIES ADDITIONAL_CLEAN_FILES "${C6C_OPT}")

# 库形态：同一份源码（src/c6c-lib.c6 = -DC6C_NO_MAIN + #include "c6c.c6"）编成
# libc6c.a / libc6c.so，导出 c6_* 嵌入 API 与 tcc_* 兼容层。它**不是** libtcc：
# 不链接 tcc、不 include tcc 的头，只是把同类能力用 c6c 自己的实现提供出来。
set(_c6c_lib_defs
    "-DC6_DEFAULT_RT=\"${C6C_DIR}/lib/rt.c6\""
    "-DC6_DEFAULT_INC=\"${C6C_DIR}/include\"")
add_custom_command(
  OUTPUT "${C6C_LIB_A}" "${C6C_LIB_SO}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${C6C_BUILD}"
  COMMAND "${CMAKE_C_COMPILER}" -x c -O2 -fPIC ${_c6c_lib_defs}
          -c -o "${C6C_BUILD}/libc6c.o" "${C6C_DIR}/src/c6c-lib.c6"
  COMMAND "${CMAKE_AR}" rcs "${C6C_LIB_A}" "${C6C_BUILD}/libc6c.o"
  COMMAND "${CMAKE_C_COMPILER}" -x c -O2 -fPIC -shared ${_c6c_lib_defs}
          -o "${C6C_LIB_SO}" "${C6C_DIR}/src/c6c-lib.c6"
  DEPENDS ${C6C_SRCS}
  COMMENT "c6c: 编出嵌入库 libc6c.a / libc6c.so（宿主 C 编译器，-DC6C_NO_MAIN）"
  VERBATIM)
add_custom_target(c6c-lib ALL DEPENDS "${C6C_LIB_A}" "${C6C_LIB_SO}")
set_target_properties(c6c-lib PROPERTIES
  ADDITIONAL_CLEAN_FILES "${C6C_LIB_A};${C6C_LIB_SO};${C6C_BUILD}/libc6c.o")

# ---------------------------------------------------------------------------
# 2. 验收 target
#
# 全部走 c6c 自己的脚本（tests/*.sh、scripts/lint.sh、bench/bench.py），区别只在
# "谁来发起"：以前是 make，现在是 CMake。脚本的口径没变，所以数字可比。
# ---------------------------------------------------------------------------

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

# --- 2.3 各后端 / 各特性的专项回归 ---
c6c_target(objcheck ENV C6C=${C6C_BIN} COMMAND ./tests/obj-check.sh)
c6c_target(ldcheck ENV C6C=${C6C_BIN} COMMAND ./tests/ld-check.sh)
c6c_target(dyncheck ENV C6C=${C6C_BIN} COMMAND ./tests/dyn-check.sh)
c6c_target(sharedcheck ENV C6C=${C6C_BIN} COMMAND ./tests/shared-check.sh)
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

# ---------------------------------------------------------------------------
# 3. 聚合门禁
#
#   fast            ≈40 s：日常改一行跑这条（外部语料也是它的一部分，不是可选）
#   check-all-c6c   ≈4 min：提交/发布前，再加自举 fixpoint / std / 诊断 / 嵌出 tcc
#   check-all-perf  再加验收 (4) 的跑分与内存门槛
#
# fast 挪出去的都是"要重跑一整遍全量编译"的：selfhost + selftest（两遍 ~12 MB
# 全量编译）、stdtest（377 个模块）、test + diagtest（120+ 用例与 316 条诊断）、
# c99test（51 条 gcc 差分，165 s）、build-tcc（编 tcc + 第三代 selfhost）。
# ---------------------------------------------------------------------------
add_custom_target(fast
  DEPENDS lint unit irtest hello ctest-c6c runflag testflag
          objcheck ldcheck dyncheck sharedcheck bcheck dbgcheck asmcheck a64check archcheck
          embedcheck pkgcheck tests2-check tests1-check c99suite llvmtest
  COMMENT "c6c: fast（快速门禁 ≈40 s，含外部语料 c99suite / 上游 tests1·tests2）")

add_custom_target(check-all-c6c
  DEPENDS unit irtest hello ctest-c6c runflag testflag
          objcheck ldcheck dyncheck sharedcheck bcheck dbgcheck asmcheck a64check archcheck
          embedcheck pkgcheck selfhost selftest stdtest diagtest c99test
          c99suite llvmtest test-c6c build-tcc tests1-check tests2-check regression-c6c
  COMMENT "c6c: check-all-c6c（全量验收：fixpoint / 全部后端 / 语料 / 编出 tcc + 第三代 selfhost）")

add_custom_target(check-all-perf
  DEPENDS check-all-c6c bench-c6c
  COMMENT "c6c: check-all-perf（全量验收 + 性能/内存硬指标）")

# ---------------------------------------------------------------------------
# 4. 安装（库形态）
# ---------------------------------------------------------------------------
if(C6_INSTALL_C6LIB)
  install(FILES "${C6C_DIR}/include/c6lib.h"
          DESTINATION "${C6_INCLUDEDIR_REL}")
  install(FILES "${C6C_LIB_A}"
          DESTINATION "${C6_LIBDIR_REL}")
  install(PROGRAMS "${C6C_LIB_SO}"
          DESTINATION "${C6_LIBDIR_REL}")
  # tcc 的嵌入 API 兼容头：装的时候用它的标准名，原本用 tcc 的程序不用改。
  # 只在**没有 tcc** 的时候装——两个都装会抢同一个 libtcc.h。
  if(NOT C6_ENABLE_TCC)
    install(FILES "${C6C_DIR}/include/tcc-compat.h"
            DESTINATION "${C6_INCLUDEDIR_REL}"
            RENAME "libtcc.h")
  endif()
endif()

# ---------------------------------------------------------------------------
# 5. CTest 视角（同一套验收，CI 按标签跑）
#
#   ctest -L c6c            验收 (1)(2)(3) + tests1 + tests2（几分钟）
#   ctest -R c6c-check-all  只跑那一条
#   ctest -L perf           验收 (4) 的性能/内存门槛（共享 runner 抖动大，
#                           CI 里用 `-LE perf` 排除，本机跑）
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
  add_test(NAME c6c-build
           COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target c6c)
  set_tests_properties(c6c-build PROPERTIES LABELS "c6c" TIMEOUT 600)

  add_test(NAME c6c-lint
           COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target lint)
  set_tests_properties(c6c-lint PROPERTIES LABELS "c6c" TIMEOUT 300)

  add_test(NAME c6c-check-all
           COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                   --target check-all-c6c)
  set_tests_properties(c6c-check-all PROPERTIES LABELS "c6c" TIMEOUT 3000)

  add_test(NAME c6c-tests2
           COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                   --target tests2-check)
  set_tests_properties(c6c-tests2 PROPERTIES LABELS "c6c" TIMEOUT 1800)

  add_test(NAME c6c-tests1
           COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                   --target tests1-check)
  set_tests_properties(c6c-tests1 PROPERTIES LABELS "c6c" TIMEOUT 1800)

  add_test(NAME c6c-bench
           COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                   --target bench-c6c)
  set_tests_properties(c6c-bench PROPERTIES LABELS "perf" TIMEOUT 1800)

  # 一条命令的入口（和仓库里 `check-install` 同一个风格）：
  #   cmake --build build --target check-c6c       验收 (1)(2)(3) + 上游 tests1/tests2
  #   cmake --build build --target check-c6c-perf  再加上验收 (4)
  add_custom_target(check-c6c
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -R "^c6c-" -LE perf
    DEPENDS c6c
    USES_TERMINAL
    COMMENT "c6c: 验收 (1)(2)(3)：fixpoint / 全部后端 / 语料 / 编出 tcc + 第三代 selfhost / 上游 tests1·tests2")

  add_custom_target(check-c6c-perf
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -R "^c6c-|^c6c$"
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -L perf
    DEPENDS c6c
    USES_TERMINAL
    COMMENT "c6c: 上面全部 + 验收 (4) 性能/内存")
endif()
