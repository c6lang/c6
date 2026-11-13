# 构建目标：编译全部由 tcc/CMakeLists.txt 的 tcc-build / tcc-build-plain 完成，
# 这里只保留 c6 侧沿用的入口名，并给 ALL 挂一个聚合点。

add_custom_target(c6 ALL)
add_dependencies(c6 tcc-build)
add_custom_target(build)
add_dependencies(build tcc-build)
add_custom_target(build-plain)
add_dependencies(build-plain tcc-build-plain)

# 配置由 tcc/CMakeLists.txt 在 CMake 配置阶段生成；参数变化只需重跑
# cmake -S . -B build，不再有单独的 bootstrap / 重新配置目标。
# tcc-clean / tcc-distclean 由 tcc/ 提供同名目标。

# 构建模式是 CMake 配置项，这几个旧入口只打印提示。
foreach(_mode IN ITEMS ext plain)
  if(_mode STREQUAL "ext")
    set(_c6_mode_flag "-DC6_ENABLE_EXT=ON")
  else()
    set(_c6_mode_flag "-DC6_ENABLE_EXT=OFF")
  endif()
  add_custom_target("mode-${_mode}"
    COMMAND "${CMAKE_COMMAND}" -E echo
      "c6: 构建模式由 CMake 决定，请重新执行 cmake -S . -B build ${_c6_mode_flag} 后再 cmake --build build"
    VERBATIM)
endforeach()

add_custom_target(c99
  COMMAND "${CMAKE_COMMAND}" -E echo
    "c6: C99 输出后端已内置在 tcc/c6（-c99 / -o *.c / -o <目录>），无需单独构建；用法见 docs/reference/cli.md"
  VERBATIM)

# test-plain 会用上游模式重建 tcc/c6 再跑 tests2，属于构建动作而不是普通
# 测试；它会替换所有 CTest 用例共用的编译器二进制，因此绝不能与 ctest
# 并发运行。保留成独立目标，不注册为 CTest 用例（也就不会被 check-all 触发）。
# 环境变量与 CTest 的 ENVIRONMENT 保持一致，输出同样落在构建目录。
set(_c6_plain_root "${CMAKE_CURRENT_BINARY_DIR}/tests/test-plain")
add_custom_target(test-plain
  COMMAND "${CMAKE_COMMAND}" -E env
    "C6_TCC_DIR=${C6_TCC_DIR_REL}" "C6_MAKE=${C6_MAKE_EXECUTABLE}"
    "C6_SELFTEST=${C6_SELFTEST}" "C6_ERRBASE=${C6_ERRBASE}"
    "C6_HOSTCC=${C6_HOSTCC}" "C6_CFLAGS=${C6_TCC_CFLAGS}"
    "C6_LLVMCC=${C6_LLVMCC}" "C6_NODE=${C6_NODE}"
    "C6_WASM_TRIPLE=${C6_WASM_TRIPLE}"
    "C6_CASE_OUT=${_c6_plain_root}"
    "C6_SELFHOST=${_c6_plain_root}/selfhost"
    "C6_BARE=${_c6_plain_root}/bare"
    "C6_TMP=${_c6_plain_root}/tmp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/regression.sh" test-plain
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "c6: 上游模式重建后跑 tests2（勿与 ctest 并发）"
  VERBATIM USES_TERMINAL)
add_dependencies(test-plain build-plain)
