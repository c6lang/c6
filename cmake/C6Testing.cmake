# CTest 是唯一的测试调度器。
#
# 用例实现仍在 tests/regression.sh（纯 shell），但注册、并发、互斥、超时、
# 日志、过滤、重跑全部交给 CTest；不再为每个用例注册一个 custom target，
# 也不再用 shell 的 xargs -P + 递归调用来做聚合。
#
#   ctest --test-dir build -L daily --output-on-failure
#   ctest --test-dir build -L regression -LE aux -j 8 --output-on-failure
#   ctest --test-dir build -R '^std-demo$' --output-on-failure
#   ctest --test-dir build --rerun-failed --output-on-failure
#
# 用例清单、分组以脚本里的 C6_STD_TARGETS / C6_CHECK_PARALLEL /
# C6_TEST_ALL 三行以及 `<case>)` 分支为单一数据源，加用例只改脚本。

set(_c6_regression_script "${CMAKE_CURRENT_SOURCE_DIR}/tests/regression.sh")
if(NOT EXISTS "${_c6_regression_script}")
  message(FATAL_ERROR "c6: 缺回归用例脚本 ${_c6_regression_script}")
endif()

# ---------------------------------------------------------------------------
# 1. 读取脚本里的分组清单（作为 CTest 标签 / 互斥组的元数据）
# ---------------------------------------------------------------------------

file(STRINGS "${_c6_regression_script}" _c6_meta_lines
     REGEX "^C6_(STD_TARGETS|CHECK_PARALLEL|TEST_ALL)=\"")
foreach(_line IN LISTS _c6_meta_lines)
  string(REGEX MATCH "^C6_([A-Z_]+)=\"([^\"]*)\"" _unused "${_line}")
  set(_meta_name "${CMAKE_MATCH_1}")
  set(_meta_value "${CMAKE_MATCH_2}")
  string(STRIP "${_meta_value}" _meta_value)
  string(REGEX REPLACE "[ \t]+" ";" _c6_group_${_meta_name} "${_meta_value}")
endforeach()
foreach(_required IN ITEMS STD_TARGETS CHECK_PARALLEL TEST_ALL)
  if(NOT DEFINED _c6_group_${_required})
    message(FATAL_ERROR "c6: tests/regression.sh 缺 C6_${_required} 分组清单")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# 2. 扫描用例分支
# ---------------------------------------------------------------------------

file(STRINGS "${_c6_regression_script}" _c6_case_lines
     REGEX "^[A-Za-z0-9_.-]+\\)$")
set(_c6_cases "")
foreach(_line IN LISTS _c6_case_lines)
  string(REGEX MATCH "^([A-Za-z0-9_.-]+)\\)$" _unused "${_line}")
  # if(MATCHES) 会覆盖 CMAKE_MATCH_1，先存进普通变量。
  set(_case "${CMAKE_MATCH_1}")
  if(NOT _case MATCHES "^__" AND NOT _case MATCHES "^(check-all|test-all|check-std-serial)$")
    list(APPEND _c6_cases "${_case}")
  endif()
endforeach()
list(REMOVE_DUPLICATES _c6_cases)

# test-plain 会重建上游模式编译器，不能和其它用例并发，也不适合当普通
# CTest 用例（构建动作不是测试）。保留成独立目标，见 C6Build.cmake。
list(REMOVE_ITEM _c6_cases "test-plain")

# aux：不属于任何分组清单、也不该进聚合门禁的用例。
#   check-tcc   只检查源码完整性
#   ubcheck     std-ub 的派生封装
#   safegate    运行时 -b 冒烟
#   bench-async 需要 g++/rustc/go/uya 等额外工具链，是有副作用的性能基准
# 它们仍可用 ctest -R / --target <用例> 单独运行。
set(_c6_aux_cases check-tcc ubcheck safegate bench-async)

# 被测 c6 程序内部写死了相对 CWD 的 build/... 路径：std/os/{io,fs,walk,
# lock,mmap,temp}.c6 与 std/net/{assets,resolver,uds}.c6 的 test 块（只有
# stdtest 的 `-test` 会执行），以及 examples/{stdkit,stdclient,stdserve,
# stdfsx}.c6 的 main。C6_CASE_OUT 覆盖不了它们，只能给这 5 个用例加一把
# 共享锁；其余 100+ 用例仍可全并发。
set(_c6_source_tree_writers
  stdtest std-kit std-client std-serve std-fsx)

# C6_CHECK_PARALLEL 是历史上验证过可并发的用例白名单：消费它打 parallel
# 标签（ctest -L parallel），并校验清单没有随用例重命名而腐烂。
foreach(_case IN LISTS _c6_group_CHECK_PARALLEL)
  if(NOT _case IN_LIST _c6_cases)
    message(FATAL_ERROR
      "c6: C6_CHECK_PARALLEL 里的用例 '${_case}' 在 regression.sh 中不存在")
  endif()
endforeach()
foreach(_case IN LISTS _c6_aux_cases _c6_source_tree_writers)
  if(NOT _case IN_LIST _c6_cases)
    message(FATAL_ERROR "c6: 标注用例 '${_case}' 在 regression.sh 中不存在")
  endif()
endforeach()

list(LENGTH _c6_cases C6_REGRESSION_CASE_COUNT)
list(LENGTH _c6_aux_cases C6_AUX_CASE_COUNT)

# ---------------------------------------------------------------------------
# 3. 注入环境：CTest 的 ENVIRONMENT 属性，替代 cmake -E env 包装
# ---------------------------------------------------------------------------

# C6_TCC_DIR_REL 由 C6Options.cmake 统一计算（C6Build.cmake 的 test-plain
# 也要用）：regression.sh 里有 `$PWD/$C6_TCC_DIR/c6` 这类写法，必须用相对
# 源码根的路径。
set(_c6_tcc_dir_env "${C6_TCC_DIR_REL}")

# 输出目录按用例隔离，在下面 foreach 里逐用例注入；这里只放真正共用的变量。
set(_c6_test_env
  "C6_TCC_DIR=${_c6_tcc_dir_env}"
  "C6_MAKE=${C6_MAKE_EXECUTABLE}"
  "C6_SELFTEST=${C6_SELFTEST}"
  "C6_ERRBASE=${C6_ERRBASE}"
  "C6_HOSTCC=${C6_HOSTCC}"
  "C6_CFLAGS=${C6_TCC_CFLAGS}"
  "C6_LLVMCC=${C6_LLVMCC}"
  "C6_NODE=${C6_NODE}"
  "C6_WASM_TRIPLE=${C6_WASM_TRIPLE}"
)
if(NOT C6_TCC_STATIC)
  set(_c6_ld_path "${C6_TCC_DIR}:$ENV{LD_LIBRARY_PATH}")
  string(REGEX REPLACE ":+$" "" _c6_ld_path "${_c6_ld_path}")
  list(APPEND _c6_test_env "LD_LIBRARY_PATH=${_c6_ld_path}")
endif()

# regression.sh 里部分 $C6_OUT/... 引用没有加引号，含空格的构建目录会让
# 这些用例以难以诊断的方式失败，这里提前给出明确提示。
if(CMAKE_CURRENT_BINARY_DIR MATCHES "[ \t;]")
  message(WARNING
    "c6: 构建目录路径含空格或分号：${CMAKE_CURRENT_BINARY_DIR}\n"
    "  regression.sh 的部分路径未加引号，请把构建目录放到无空格路径下。")
endif()

set(_c6_ctest_jobs "${C6_CHECK_JOBS}")
if("${_c6_ctest_jobs}" STREQUAL "")
  set(_c6_ctest_jobs "${C6_TCC_JOBS}")
endif()

# ---------------------------------------------------------------------------
# 4. 注册测试
# ---------------------------------------------------------------------------

if(BUILD_TESTING)
  # ---- 日常 5 个：直接调测试运行器，不走回归脚本 ----
  set(_c6_daily_env ${_c6_test_env}
    "TCC=${C6_TCC_DIR}/c6${C6_TCC_EXESUF}"
    "C99TCC=${C6_TCC_DIR}/c6${C6_TCC_EXESUF}"
  )
  add_test(NAME c6-selftest
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/run-tests.sh")
  add_test(NAME c6-errcheck
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/run-errbase.sh")
  add_test(NAME c6-tests2
           COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6_TCC_DIR}/tests/tests2" tests2.all)
  add_test(NAME c6-c99-selftest
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/run-tests.sh")
  add_test(NAME c6-c99tcc-selftest
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/run-tests.sh")
  set_tests_properties(c6-selftest c6-errcheck c6-tests2
                       c6-c99-selftest c6-c99tcc-selftest PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    ENVIRONMENT "${_c6_daily_env}"
    LABELS "daily"
    TIMEOUT 1800)
  # 与回归矩阵里的 `test` 共用一把锁：两者都会跑 tcc/tests/tests2 的 make。
  set_tests_properties(c6-tests2 PROPERTIES RESOURCE_LOCK "c6-tests2")
  set_tests_properties(c6-c99-selftest PROPERTIES
    ENVIRONMENT "${_c6_daily_env};BACKEND=c99")
  set_tests_properties(c6-c99tcc-selftest PROPERTIES
    ENVIRONMENT "${_c6_daily_env};BACKEND=c99;C99CC=${C6_TCC_DIR}/c6${C6_TCC_EXESUF};C99FLAGS=-B ${C6_TCC_DIR} -std=c99 -w")

  # ---- 回归矩阵：每个用例一个原生 CTest 测试 ----
  foreach(_case IN LISTS _c6_cases)
    set(_case_root "${CMAKE_CURRENT_BINARY_DIR}/tests/${_case}")
    file(MAKE_DIRECTORY "${_case_root}")
    add_test(NAME "${_case}"
             COMMAND "${_c6_regression_script}" "${_case}")

    # 每个用例独占一套输出目录，因此不再需要按输出路径整组串行。
    set(_case_env ${_c6_test_env}
      "C6_CASE_OUT=${_case_root}"
      "C6_SELFHOST=${_case_root}/selfhost"
      "C6_BARE=${_case_root}/bare"
      "C6_TMP=${_case_root}/tmp")

    set(_labels "regression")
    set(_locks "")
    if(_case IN_LIST _c6_aux_cases)
      list(APPEND _labels "aux")
    endif()
    if(_case IN_LIST _c6_group_CHECK_PARALLEL)
      list(APPEND _labels "parallel")
    endif()
    if(_case IN_LIST _c6_group_STD_TARGETS)
      list(APPEND _labels "std")
    endif()
    if(_case IN_LIST _c6_group_TEST_ALL)
      list(APPEND _labels "core")
    endif()
    if(_case MATCHES "^(selfhost|c99selfhost|c99selfhost-tcc|c6selfcheck)$")
      list(APPEND _labels "selfhost")
    elseif(_case MATCHES "^fstest")
      # fstest-qemu 会递归调用前几个 fstest 用例，环境变量继承同一输出目录。
      list(APPEND _labels "freestanding")
      list(APPEND _locks "c6-freestanding")
    elseif(_case MATCHES "^(test|tests2)$")
      # 上游 tests2 的 make 会往源码树写临时产物，仍需互斥。
      list(APPEND _labels "tests2")
      list(APPEND _locks "c6-tests2")
    elseif(_case MATCHES "^(c99test|c99tcc-test)$")
      list(APPEND _labels "backend")
    endif()
    if(_case IN_LIST _c6_source_tree_writers)
      list(APPEND _labels "source-tree")
      list(APPEND _locks "c6-source-tree-build")
    endif()

    set_tests_properties("${_case}" PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      ENVIRONMENT "${_case_env}"
      LABELS "${_labels}"
      TIMEOUT 1800)
    foreach(_lock IN LISTS _locks)
      set_property(TEST "${_case}" APPEND PROPERTY RESOURCE_LOCK "${_lock}")
    endforeach()
  endforeach()
endif()

# ---------------------------------------------------------------------------
# 5. 兼容入口：无论 BUILD_TESTING 是否开启都保留同名目标
# ---------------------------------------------------------------------------
# 文档与旧流程里大量使用 `cmake --build build --target <用例>`。开启测试时
# 这些目标只转发给同一个 CTest 用例，保证「只有一个调度器」；关闭测试时
# 给出明确提示，而不是让目标无声消失。新代码应直接用 ctest -R / -L。

set(_c6_reserved_targets
  test all clean help install package package_source
  build build-plain c6 c99
  wasmrt wasmtest distclean mode-ext mode-plain
  check-install c6-clean clean-all c6-help tcc-clean tcc-distclean
  check-all test-all check-std-serial test-plain check-aux)
foreach(_case IN LISTS _c6_cases)
  if(NOT _case IN_LIST _c6_reserved_targets)
    if(BUILD_TESTING)
      add_custom_target("${_case}"
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_CURRENT_BINARY_DIR}"
                -R "^${_case}$" --output-on-failure
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "c6: ctest -R ^${_case}$（兼容入口）"
        VERBATIM USES_TERMINAL)
    else()
      add_custom_target("${_case}"
        COMMAND "${CMAKE_COMMAND}" -E echo
          "c6: 测试未注册，请用 -DBUILD_TESTING=ON 重新配置后再跑 ${_case}"
        VERBATIM)
    endif()
    add_dependencies("${_case}" c6)
  endif()
endforeach()

# ---------------------------------------------------------------------------
# 6. 聚合入口：薄壳，只负责选标签和并发度，调度仍是 ctest
# ---------------------------------------------------------------------------

function(c6_add_ctest_target target)
  if(NOT BUILD_TESTING)
    add_custom_target("${target}"
      COMMAND "${CMAKE_COMMAND}" -E echo
        "c6: 测试未注册，请用 -DBUILD_TESTING=ON 重新配置"
      VERBATIM)
    return()
  endif()
  cmake_parse_arguments(_arg "" "JOBS;LABEL;EXCLUDE_LABEL" "" ${ARGN})
  set(_cmd "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_CURRENT_BINARY_DIR}"
      --output-on-failure)
  if(_arg_LABEL)
    list(APPEND _cmd -L "${_arg_LABEL}")
  endif()
  if(_arg_EXCLUDE_LABEL)
    list(APPEND _cmd -LE "${_arg_EXCLUDE_LABEL}")
  endif()
  if(_arg_JOBS)
    list(APPEND _cmd -j "${_arg_JOBS}")
  endif()
  add_custom_target("${target}" COMMAND ${_cmd}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "c6: ctest ${target}"
    VERBATIM USES_TERMINAL)
  add_dependencies("${target}" c6)
endfunction()

c6_add_ctest_target(check-all LABEL regression EXCLUDE_LABEL aux
                    JOBS "${_c6_ctest_jobs}")
c6_add_ctest_target(test-all LABEL core EXCLUDE_LABEL aux
                    JOBS "${_c6_ctest_jobs}")
c6_add_ctest_target(check-std-serial LABEL std JOBS 1)
c6_add_ctest_target(check-aux LABEL aux JOBS 1)
