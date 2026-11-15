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
set(C6C_BIN "${C6C_DIR}/build/c6c")

# 引导编译器（stage0）：
#   C6_ENABLE_TCC=ON（默认）→ 仓库自己的 tcc/c6，-B tcc 带 crt / libtcc1.a；
#   C6_ENABLE_TCC=OFF        → 宿主 C 编译器，`-x c` 直接编 c6c 的单 TU 源码。
# 两条路的产物同一份源码、同一套语义，验收（fixpoint / 三后端 / 语料）与
# stage0 无关；OFF 这条路**不需要任何 tcc 构建产物**。
if(C6_ENABLE_TCC)
  set(C6C_STAGE0 "${C6_TCC_DIR}/c6${C6_TCC_EXESUF}")
  set(_c6c_bootstrap_args "C6C=${C6C_STAGE0}" "BDIR=${C6_TCC_DIR}")
  set(_c6c_stage0_dep "${C6C_STAGE0}")
else()
  set(C6C_STAGE0 "${CMAKE_C_COMPILER}")
  set(_c6c_bootstrap_args "BOOTSTRAP=host" "HOSTCC=${CMAKE_C_COMPILER}")
  set(_c6c_stage0_dep "")
endif()

# 引导编译器 tcc/c6 在 shared 形态（-DC6_TCC_SHARED_LIBTCC=ON）下自己就是链到
# libtcc.so 的：构建树里的 c6 没有 rpath，直接执行会报
#   tcc/c6: error while loading shared libraries: libtcc.so
# 这正是 CI shared=ON 矩阵里 `cmake --build` 挂在 c6c 那一步的原因。所以凡是
# 要**执行** c6 的地方（c6c 的自定义命令 + 下面几个 c6c-* 用例）都补上
# LD_LIBRARY_PATH，口径和 C6Testing.cmake 里注给其它用例的那套一致。
set(_c6c_ld_env "")
if(NOT C6_TCC_STATIC)
  set(_c6c_ld_path "${C6_TCC_DIR}:$ENV{LD_LIBRARY_PATH}")
  string(REGEX REPLACE ":+$" "" _c6c_ld_path "${_c6c_ld_path}")
  set(_c6c_ld_env "LD_LIBRARY_PATH=${_c6c_ld_path}")
endif()
if(_c6c_ld_env)
  set(_c6c_make_cmd "${CMAKE_COMMAND}" -E env "${_c6c_ld_env}"
                    "${C6_MAKE_EXECUTABLE}")
  set(_c6c_test_env "ENVIRONMENT" "${_c6c_ld_env}")
else()
  set(_c6c_make_cmd "${C6_MAKE_EXECUTABLE}")
  set(_c6c_test_env "")
endif()

file(GLOB C6C_SRCS CONFIGURE_DEPENDS "${C6C_DIR}/src/*.c6")

if(EXISTS "${C6C_DIR}/Makefile")
  add_custom_command(
    OUTPUT "${C6C_BIN}"
    COMMAND ${_c6c_make_cmd} -C "${C6C_DIR}" build ${_c6c_bootstrap_args}
    DEPENDS ${C6C_SRCS} ${_c6c_stage0_dep}
    COMMENT "c6c: 用 ${C6C_STAGE0} 编出自举编译器 (c6c/build/c6c)"
    VERBATIM)

  add_custom_target(c6c ALL DEPENDS "${C6C_BIN}")
  # 引导编译器是 tcc/c6 时，它是 tcc-build 的产物：显式连上依赖，
  # `--target c6c` 从干净目录也能一次编到底。stage0 换成宿主 cc 时没有这个
  # 依赖，c6c 也从 tcc 的构建链里彻底摘出来。
  if(C6_ENABLE_TCC)
    add_dependencies(c6c tcc-build)
  else()
    # 不建 tcc，但 tcc 的**配置文件**还是要生成：c6c 的 build-tcc 把
    # tcc/*.c 当语料编，那份源码 `#include "config.h"`。tcc-config 只同步
    # config.mak / config.h / config.texi，不碰 tcc 的编译产物。
    add_dependencies(c6c tcc-config)
    add_dependencies(c6 c6c)
    add_dependencies(build c6c)
  endif()
  set_target_properties(c6c PROPERTIES
    ADDITIONAL_CLEAN_FILES "${C6C_BIN}")

  # 库形态：同一份源码（src/c6c-lib.c6，用 -DC6C_NO_MAIN 关掉 main()）编成
  # libc6c.a / libc6c.so，导出 c6_* 那套嵌入 API 和 tcc_* 兼容层。
  # 它**不是** libtcc：不链接 tcc、不 include tcc 的头，只是把同一类能力
  # （把编译器嵌进宿主程序）用 c6c 自己的实现提供出来。
  set(C6C_LIB_A "${C6C_DIR}/build/libc6c.a")
  set(C6C_LIB_SO "${C6C_DIR}/build/libc6c.so")
  add_custom_command(
    OUTPUT "${C6C_LIB_A}" "${C6C_LIB_SO}"
    COMMAND ${_c6c_make_cmd} -C "${C6C_DIR}" libc6c
            "HOSTCC=${CMAKE_C_COMPILER}" "BUILD=build"
    DEPENDS ${C6C_SRCS} "${C6C_DIR}/Makefile"
    COMMENT "c6c: 编出嵌入库 libc6c.a / libc6c.so（宿主 C 编译器，-DC6C_NO_MAIN）"
    VERBATIM)
  add_custom_target(c6c-lib ALL DEPENDS "${C6C_LIB_A}" "${C6C_LIB_SO}")

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
           COMMAND ${_c6c_make_cmd} -C "${C6C_DIR}" build
                   ${_c6c_bootstrap_args})
  set_tests_properties(c6c-build PROPERTIES
    WORKING_DIRECTORY "${C6C_DIR}"
    LABELS "c6c"
    TIMEOUT 600 ${_c6c_test_env})

  # 目录边界检查（不 include tcc 头、不链接 libtcc、不引用 tcc 的 .c、不出现
  # LGPL 头、必需文件齐全）。纯 shell + grep，几百毫秒。
  add_test(NAME c6c-lint
           COMMAND ${_c6c_make_cmd} -C "${C6C_DIR}" lint)
  set_tests_properties(c6c-lint PROPERTIES
    WORKING_DIRECTORY "${C6C_DIR}"
    LABELS "c6c"
    TIMEOUT 300 ${_c6c_test_env})

  # 验收 (1)(2)(3)：自举 fixpoint 逐字节、三后端同一套语料、tests+std+诊断、
  # c6c 编出上游 tcc 并让它跑 tests2（含**第三代 selfhost**）。
  add_test(NAME c6c-check-all
           COMMAND ${_c6c_make_cmd} -C "${C6C_DIR}" check-all
                   "ROOT=${CMAKE_CURRENT_SOURCE_DIR}" ${_c6c_bootstrap_args})
  set_tests_properties(c6c-check-all PROPERTIES
    WORKING_DIRECTORY "${C6C_DIR}"
    LABELS "c6c"
    TIMEOUT 2400 ${_c6c_test_env})

  # 验收 (3) 的闸门：tcc 上游 tests2 逐条比对，`c6c/tests/tcc-na.txt` 里登记的
  # 算"架构性不可达"，不在表里的失败一律算意外失败、这条就红。
  add_test(NAME c6c-tests2
           COMMAND ${_c6c_make_cmd} -C "${C6C_DIR}" tests2-check
                   "ROOT=${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(c6c-tests2 PROPERTIES
    WORKING_DIRECTORY "${C6C_DIR}"
    LABELS "c6c"
    TIMEOUT 1800 ${_c6c_test_env})

  # 验收 (3) 的另一半：tcc 上游 tests/ 下那批老式单文件用例。
  add_test(NAME c6c-tests1
           COMMAND ${_c6c_make_cmd} -C "${C6C_DIR}" tests1-check
                   "ROOT=${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(c6c-tests1 PROPERTIES
    WORKING_DIRECTORY "${C6C_DIR}"
    LABELS "c6c"
    TIMEOUT 1800 ${_c6c_test_env})

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
           COMMAND ${_c6c_make_cmd} -C "${C6C_DIR}" bench-c6c
                   "ROOT=${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(c6c-bench PROPERTIES
    WORKING_DIRECTORY "${C6C_DIR}"
    LABELS "perf"
    TIMEOUT 1800 ${_c6c_test_env})
endif()

# ---------------------------------------------------------------------------
# c6c 替换 c6 的两个聚合门禁：fast / check-all-c6c
#
# 主仓原来的门禁是 `check-all`（被测编译器是 tcc/c6，见 C6Testing.cmake）。这
# 两条把被测编译器换成 c6c 自己：自举 fixpoint、三后端、全部语料都由 c6c 跑。
# 第三方语料是这两条门禁的一部分，不是可选装饰：
#   c99suite      third_party/c-testsuite 的 150 条外部 C99 用例（语言层闸门）
#   tests1-check  tcc 上游 tests/ 那批老式单文件用例
#   tests2-check  tcc 上游 tests2 逐条比对（tests/tcc-na.txt 之外不许有意外失败）
#   build-tcc     c6c 编出上游 tcc，再由它跑上游 tests2 + 第三代 selfhost
#
#   cmake --build build --target fast           ≈40 s，日常改一行就够
#   cmake --build build --target check-all-c6c  ≈4 min，提交/发布前
#
# 两条都直调 c6c/Makefile（fast / check-all + 上游那两条闸门），不走 ctest：
# 这样 `make -C c6c fast` 和 `--target fast` 跑的是同一套脚本、同一批数字。
# ctest 视角的同一套验收仍然是 `check-c6c`（拆成 6 条用例，CI 按标签跑）。
# 不设 BUILD_TESTING 守卫：它们跑的是 c6c 自己的脚本，跟测试注册无关。
if(EXISTS "${C6C_DIR}/Makefile")
  set(_c6c_gate_args
    "ROOT=${CMAKE_CURRENT_SOURCE_DIR}" "C6C=${C6C_STAGE0}" "BDIR=${C6_TCC_DIR}")

  add_custom_target(fast
    COMMAND ${_c6c_make_cmd} -C "${C6C_DIR}" fast ${_c6c_gate_args}
    DEPENDS c6c
    USES_TERMINAL
    COMMENT "c6c: fast（c6c 替换 c6 的快速门禁 ≈40 s，含外部 c99suite / 上游 tests1·tests2）")

  add_custom_target(check-all-c6c
    COMMAND ${_c6c_make_cmd} -C "${C6C_DIR}" check-all ${_c6c_gate_args}
    COMMAND ${_c6c_make_cmd} -C "${C6C_DIR}" tests1-check tests2-check ${_c6c_gate_args}
    DEPENDS c6c
    USES_TERMINAL
    COMMENT "c6c: check-all-c6c（c6c 替换 c6 的全量门禁，含外部语料 c-testsuite / 上游 tests1·tests2 / c6c 编出的 tcc）")
endif()
