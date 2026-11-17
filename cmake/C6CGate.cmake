# c6c 的聚合门禁 + CTest 注册。
#
# 由 cmake/C6C.cmake 在 C6CCheck.cmake 之后 include（依赖那里定义的全部
# 验收 target 名字）。
#
#   fast            ≈40 s：日常改一行跑这条（外部语料也是它的一部分，不是可选）
#   check-all-c6c   ≈4 min：提交/发布前，再加自举 fixpoint / std / 诊断 / 嵌出 tcc
#   check-all-perf  再加验收 (4) 的跑分与内存门槛
#
# fast 挪出去的都是"要重跑一整遍全量编译"的：selfhost + selftest（两遍 ~12 MB
# 全量编译）、stdtest（381 个模块）、test + diagtest（120+ 用例与 316 条诊断）、
# c99test（51 条 gcc 差分，165 s）、build-tcc（编 tcc + 第三代 selfhost）。

add_custom_target(fast
  DEPENDS lint unit irtest hello ctest-c6c runflag testflag pmtest stage1check
          objcheck ldcheck dyncheck sharedcheck printfcheck esccheck tmplcheck bcheck dbgcheck asmcheck a64check archcheck
          embedcheck pkgcheck tests2-check tests1-check ctutest c99suite ${C6C_LLVM_TARGETS}
  COMMENT "c6c: fast（快速门禁 ≈40 s，含外部语料 c99suite / 上游 tests1·tests2）")

add_custom_target(check-all-c6c
  DEPENDS unit irtest hello ctest-c6c runflag testflag pmtest stage1check
          objcheck ldcheck dyncheck sharedcheck printfcheck esccheck tmplcheck bcheck dbgcheck asmcheck a64check archcheck
          embedcheck pkgcheck ctutest selfhost selftest stdtest diagtest c99test
          c99suite ${C6C_LLVM_TARGETS} test-c6c build-tcc tests1-check tests2-check regression-c6c
  COMMENT "c6c: check-all-c6c（全量验收：fixpoint / 全部后端 / 语料 / 编出 tcc + 第三代 selfhost）")

add_custom_target(check-all-perf
  DEPENDS check-all-c6c bench-c6c
  COMMENT "c6c: check-all-perf（全量验收 + 性能/内存硬指标）")

# ---------------------------------------------------------------------------
# CTest 视角（同一套验收，CI 按标签跑）
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

  add_test(NAME c6c-pm
           COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target pmtest)
  set_tests_properties(c6c-pm PROPERTIES LABELS "c6c" TIMEOUT 600)

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
