# 辅助目标：清理与帮助。tcc/ 是上游源码，清理仍调用它的 Makefile（不改）。

# 回归用例的产物都落在 CMake 构建目录下（每用例一个 tests/<用例>/ 子目录），
# 用 CMAKE_CURRENT_BINARY_DIR 而不是硬编码源码树里的 build/。
set(_c6_tree_artifacts "${CMAKE_CURRENT_BINARY_DIR}/tests")

add_custom_target(c6-clean
  COMMAND "${CMAKE_COMMAND}" -E rm -rf ${_c6_tree_artifacts}
  COMMAND sh -c "rm -f '${C6_TCC_DIR}'/libtcc1-*.a"
  COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6_TCC_DIR}" clean
  COMMENT "c6: 清理自举/裸机/交叉产物与 tcc/ 目标文件"
  VERBATIM USES_TERMINAL)

add_custom_target(clean-all
  COMMAND "${CMAKE_COMMAND}" -E rm -rf ${_c6_tree_artifacts}
  COMMAND sh -c "rm -f '${C6_TCC_DIR}'/libtcc1-*.a"
  COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6_TCC_DIR}" distclean
  COMMAND "${CMAKE_COMMAND}" -E rm -f
          "${CMAKE_CURRENT_BINARY_DIR}/tcc/generated/config.applied"
  COMMENT "c6: 连生成的 config.mak/config.h 一起清理（之后需重新 cmake -S . -B build）"
  VERBATIM USES_TERMINAL)
add_custom_target(distclean)
add_dependencies(distclean clean-all)

# `help` 是 CMake 内置目标（`--target help` 列出全部 target）；c6-help 打印
# 常用命令与用例分组，文本单独放在 cmake/C6Help.txt，不再依赖任何 Makefile。
add_custom_target(c6-help
  COMMAND "${CMAKE_COMMAND}" -E cat "${CMAKE_CURRENT_SOURCE_DIR}/cmake/C6Help.txt"
  VERBATIM USES_TERMINAL)
