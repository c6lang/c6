# c6c —— 用 c6 语言写的自举编译器（独立子目录 `c6c/`，native / C99 / LLVM IR 三后端）
#
# 这一步只做两件事：
#   1. 把 c6c 编出来（`cmake --build` 顺带就编，`--target c6c` 单独编）；
#   2. 把它的验收挂到 CTest 上（label `c6c`，性能那条单独挂 `c6c-perf`）。
#
# 为什么还留着 `c6c/Makefile`：验收本身是一串"跑测试、对输出、算 fixpoint"的
# 脚本，Make 里写着 20 多个目标（check-all / selfhost / tests2-check / bench…），
# 这些不是"编译一个目标"能表达的。所以这边只把**编译**和**入口**接进 CMake，
# 细节仍然由 c6c/Makefile 承担——`make -C c6c check-all` 单独跑也照样能用。
#
# 引导编译器是仓库自己的 stage0 `tcc/c6`（C6_TCC_DIR 下的那个），所以 c6c 的
# 编译天然依赖它：tcc 先编好，c6c 才有得编。

set(C6C_DIR "${CMAKE_CURRENT_SOURCE_DIR}/c6c")
set(C6C_STAGE0 "${C6_TCC_DIR}/c6${C6_TCC_EXESUF}")
set(C6C_BIN "${C6C_DIR}/build/c6c")

file(GLOB C6C_SRCS CONFIGURE_DEPENDS "${C6C_DIR}/src/*.c6")

if(EXISTS "${C6C_DIR}/Makefile")
  add_custom_command(
    OUTPUT "${C6C_BIN}"
    COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6C_DIR}" build
            "C6C=${C6C_STAGE0}" "BDIR=${C6_TCC_DIR}"
    DEPENDS ${C6C_SRCS} "${C6C_STAGE0}"
    COMMENT "c6c: 用 ${C6C_STAGE0} 编出自举编译器 (c6c/build/c6c)"
    VERBATIM)

  add_custom_target(c6c ALL DEPENDS "${C6C_BIN}")
  # 引导编译器 tcc/c6 是 tcc-build 的产物：显式连上依赖，`--target c6c` 从
  # 干净目录也能一次编到底。
  add_dependencies(c6c tcc-build)
  set_target_properties(c6c PROPERTIES
    ADDITIONAL_CLEAN_FILES "${C6C_BIN}")
endif()

# ---------------------------------------------------------------------------
# 验收目标（CTest）
#
#   ctest -L c6c           验收 (1)(2)(3) + tests1 + tests2 闸门（几分钟）
#   ctest -R c6c-check-all 只跑那一条
#   ctest -L perf          验收 (4) 的性能/内存门槛（共享 runner 抖动大，
#                          CI 里用 `-LE perf` 排除，本机跑）
# ---------------------------------------------------------------------------
if(BUILD_TESTING AND EXISTS "${C6C_DIR}/Makefile")
  add_test(NAME c6c-build
           COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6C_DIR}" build
                   "C6C=${C6C_STAGE0}" "BDIR=${C6_TCC_DIR}")
  set_tests_properties(c6c-build PROPERTIES
    WORKING_DIRECTORY "${C6C_DIR}"
    LABELS "c6c"
    TIMEOUT 600)

  # 验收 (1)(2)(3)：自举 fixpoint 逐字节、三后端同一套语料、tests+std+诊断、
  # c6c 编出上游 tcc 并让它跑 tests2（含**第三代 selfhost**）。
  add_test(NAME c6c-check-all
           COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6C_DIR}" check-all
                   "ROOT=${CMAKE_CURRENT_SOURCE_DIR}" "C6C=${C6C_STAGE0}"
                   "BDIR=${C6_TCC_DIR}")
  set_tests_properties(c6c-check-all PROPERTIES
    WORKING_DIRECTORY "${C6C_DIR}"
    LABELS "c6c"
    TIMEOUT 2400)

  # 验收 (3) 的闸门：tcc 上游 tests2 逐条比对，`c6c/tests/tcc-na.txt` 里登记的
  # 算"架构性不可达"，不在表里的失败一律算意外失败、这条就红。
  add_test(NAME c6c-tests2
           COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6C_DIR}" tests2-check
                   "ROOT=${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(c6c-tests2 PROPERTIES
    WORKING_DIRECTORY "${C6C_DIR}"
    LABELS "c6c"
    TIMEOUT 1800)

  # 验收 (3) 的另一半：tcc 上游 tests/ 下那批老式单文件用例。
  add_test(NAME c6c-tests1
           COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6C_DIR}" tests1-check
                   "ROOT=${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(c6c-tests1 PROPERTIES
    WORKING_DIRECTORY "${C6C_DIR}"
    LABELS "c6c"
    TIMEOUT 1800)

  # 一条命令的入口（和仓库里 `check-install` 同一个风格）：
  #   cmake --build build --target check-c6c       验收 (1)(2)(3) + 上游 tests1/tests2
  #   cmake --build build --target check-c6c-perf  再加上验收 (4)
  add_custom_target(check-c6c
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -R "^c6c-" -LE perf
    DEPENDS c6c
    USES_TERMINAL
    COMMENT "c6c: 验收 (1)(2)(3)：fixpoint / 三后端 / 语料 / 编出 tcc + 第三代 selfhost / 上游 tests1·tests2")

  add_custom_target(check-c6c-perf
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -R "^c6c-|^c6c$"
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -L perf
    DEPENDS c6c
    USES_TERMINAL
    COMMENT "c6c: 上面全部 + 验收 (4) 性能/内存")

  # 验收 (4)：性能与内存门槛。门槛是按开发机定的，CI 的共享 runner 抖动 ±10%
  # 会误报，所以单独挂 `perf`，CI 里用 `-LE perf` 排除。
  add_test(NAME c6c-bench
           COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6C_DIR}" bench-c6c
                   "ROOT=${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(c6c-bench PROPERTIES
    WORKING_DIRECTORY "${C6C_DIR}"
    LABELS "perf"
    TIMEOUT 1800)
endif()
