# c6 配置参数与安装路径：所有 cache 变量、平台检查、路径归一化。
# 只在 CMakeLists.txt 的顶层作用域被 include 一次。

# ---------------------------------------------------------------------------
# 1. 从 c6 侧传入的参数（传给 tcc/CMakeLists.txt，不改 tcc 源码）
# ---------------------------------------------------------------------------

set(C6_TCC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tcc" CACHE PATH
    "tcc 源码树；其中的 CMakeLists.txt 生成配置、Makefile 负责编译")
option(C6_ENABLE_EXT "给 tcc/Makefile 传 EXTRA-DEFS=-DC6_EXT" ON)
set(C6_EXTRA_DEFS "" CACHE STRING
    "追加到 EXTRA-DEFS 的宏，空格分隔，例如 -DC6_EXT -DFOO")
set(C6_TCC_CFLAGS "-g -O0 -Wall -Wno-unused-result" CACHE STRING
    "传给 tcc/Makefile 的 CFLAGS")
set(C6_TCC_CC "" CACHE STRING
    "tcc/CMakeLists.txt 用的 C 编译器；留空 = CMAKE_C_COMPILER")
set(C6_TCC_CONFIG_VARS "" CACHE STRING
    "追加 CONFIG_x=y 配置变量，CMake 列表（分号分隔），如 CONFIG_rpath=no")
set(C6_TCC_MAKE_ARGS "" CACHE STRING
    "追加给 tcc/Makefile 的 VAR=值，CMake 列表（分号分隔）")
option(C6_TCC_SHARED_LIBTCC "CONFIG_static=no，产出 libtcc.so" OFF)
option(C6_INSTALL_CMAKE_PACKAGE "安装 c6Config.cmake / c6Targets.cmake" ON)
option(C6_INSTALL_PKGCONF "安装 c6.pc" ON)
option(C6_INSTALL_LIBTCC_PC "额外安装 libtcc.pc 兼容名" ON)
option(C6_INSTALL_TCC_SYMLINK "安装 bindir/tcc -> c6 兼容软链" ON)
option(C6_INSTALL_DOCS "安装 c6 的 docs/、README、CHANGELOG 与 license" ON)
set(C6_INSTALL_TCCDIR "" CACHE STRING
    "运行时库目录；留空 = <CMAKE_INSTALL_FULL_LIBDIR>/tcc；相对值按安装前缀解析")
set(C6_INSTALL_DOCDIR "" CACHE STRING
    "上游 tcc 文档目录（tcc-doc.html）；留空 = <CMAKE_INSTALL_FULL_DATAROOTDIR>/doc/tcc；相对值按安装前缀解析")
set(C6_INSTALL_C6_DOCDIR "" CACHE STRING
    "c6 自己的文档目录；留空 = <CMAKE_INSTALL_FULL_DATAROOTDIR>/doc/c6；相对值按安装前缀解析")

ProcessorCount(_c6_ncpu)
if(_c6_ncpu EQUAL 0)
  set(_c6_ncpu 4)
endif()
set(C6_TCC_JOBS "${_c6_ncpu}" CACHE STRING "子 make 的 -j 并发数")

# 回归用例脚本 tests/regression.sh 用到的变量。C6Testing.cmake 通过
# CTest 的 ENVIRONMENT 属性注入；脚本里都带同名默认值，单独运行也能工作。
set(C6_HOSTCC "gcc" CACHE STRING
    "自举 / c6selfcheck 的宿主编译器（原 HOSTCC）")
set(C6_SELFTEST "tests/run-tests.sh" CACHE STRING "自测脚本（原 SELFTEST）")
set(C6_ERRBASE "tests/run-errbase.sh" CACHE STRING "errbase 脚本（原 ERRBASE）")
set(C6_CHECK_JOBS "" CACHE STRING
    "check-all / test-all 的 CTest -j 并发数；留空 = C6_TCC_JOBS")
set(C6_LLVMCC "" CACHE STRING "wasm 运行库用的 clang；留空交给脚本探测")
set(C6_LLVMAR "" CACHE STRING "wasm 运行库用的 llvm-ar；留空交给脚本探测")
set(C6_NODE "" CACHE STRING "wasm 冒烟测试用的 node；留空交给脚本探测")
set(C6_WASM_TRIPLE "" CACHE STRING
    "wasm 目标 triple；留空则用 scripts/ 脚本自己的默认值")

find_program(C6_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
if(NOT EXISTS "${C6_TCC_DIR}/CMakeLists.txt"
   OR NOT EXISTS "${C6_TCC_DIR}/Makefile")
  message(FATAL_ERROR
    "找不到 tcc 源码树：${C6_TCC_DIR}/CMakeLists.txt 或 Makefile 缺失。")
endif()

# tcc 树允许放在仓库外（C6_TCC_DIR 是绝对路径），但 regression.sh 与
# test-plain 里有 `$PWD/$C6_TCC_DIR/c6` 这类写法，运行环境需要相对路径。
file(RELATIVE_PATH C6_TCC_DIR_REL
     "${CMAKE_CURRENT_SOURCE_DIR}" "${C6_TCC_DIR}")

# 把绝对安装目录换成相对 prefix 的形式，交给 install() 用；这样
# cmake --install --prefix 与 DESTDIR 都能正常工作。
function(c6_prefix_relative out path)
  if(IS_ABSOLUTE "${path}")
    file(RELATIVE_PATH _rel "${CMAKE_INSTALL_PREFIX}" "${path}")
  else()
    set(_rel "${path}")
  endif()
  set(${out} "${_rel}" PARENT_SCOPE)
endfunction()

# 归一化写回 cache，而不是用普通 set() 遮蔽用户传入的 cache 值：后者会让
# 第二次 configure 又读到未归一化的旧值，CMakeCache 里也看不出 -D 的结果。
set(_c6_tccdir_doc
    "运行时库目录；留空 = <CMAKE_INSTALL_FULL_LIBDIR>/tcc；相对值按安装前缀解析")
set(_c6_docdir_doc
    "上游 tcc 文档目录（tcc-doc.html）；留空 = <CMAKE_INSTALL_FULL_DATAROOTDIR>/doc/tcc；相对值按安装前缀解析")
set(_c6_c6docdir_doc
    "c6 自己的文档目录；留空 = <CMAKE_INSTALL_FULL_DATAROOTDIR>/doc/c6；相对值按安装前缀解析")
if(NOT C6_INSTALL_TCCDIR)
  set(C6_INSTALL_TCCDIR "${CMAKE_INSTALL_FULL_LIBDIR}/tcc"
      CACHE STRING "${_c6_tccdir_doc}" FORCE)
elseif(NOT IS_ABSOLUTE "${C6_INSTALL_TCCDIR}")
  set(C6_INSTALL_TCCDIR "${CMAKE_INSTALL_PREFIX}/${C6_INSTALL_TCCDIR}"
      CACHE STRING "${_c6_tccdir_doc}" FORCE)
endif()
if(NOT C6_INSTALL_DOCDIR)
  set(C6_INSTALL_DOCDIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/doc/tcc"
      CACHE STRING "${_c6_docdir_doc}" FORCE)
elseif(NOT IS_ABSOLUTE "${C6_INSTALL_DOCDIR}")
  set(C6_INSTALL_DOCDIR "${CMAKE_INSTALL_PREFIX}/${C6_INSTALL_DOCDIR}"
      CACHE STRING "${_c6_docdir_doc}" FORCE)
endif()
if(NOT C6_INSTALL_C6_DOCDIR)
  set(C6_INSTALL_C6_DOCDIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/doc/c6"
      CACHE STRING "${_c6_c6docdir_doc}" FORCE)
elseif(NOT IS_ABSOLUTE "${C6_INSTALL_C6_DOCDIR}")
  set(C6_INSTALL_C6_DOCDIR "${CMAKE_INSTALL_PREFIX}/${C6_INSTALL_C6_DOCDIR}"
      CACHE STRING "${_c6_c6docdir_doc}" FORCE)
endif()

# install(DESTINATION) 与 configure_package_config_file(PATH_VARS) 都以
# 「目录位于 CMAKE_INSTALL_PREFIX 内」为前提；prefix 外的绝对路径会让
# DESTDIR / CPack 的内容落到前缀之外，c6Config.cmake 也无法重定位。
foreach(_c6_dir IN ITEMS C6_INSTALL_TCCDIR C6_INSTALL_DOCDIR C6_INSTALL_C6_DOCDIR)
  file(RELATIVE_PATH _c6_dir_rel "${CMAKE_INSTALL_PREFIX}" "${${_c6_dir}}")
  if(_c6_dir_rel MATCHES "^\\.\\./" OR IS_ABSOLUTE "${_c6_dir_rel}")
    message(FATAL_ERROR
      "c6: ${_c6_dir}=${${_c6_dir}} 不在安装前缀 ${CMAKE_INSTALL_PREFIX} 内。"
      "请改用相对路径，或把 CMAKE_INSTALL_PREFIX 设为包含它的位置。")
  endif()
endforeach()
set(C6_PKGCONFIG_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig")

c6_prefix_relative(C6_BINDIR_REL "${CMAKE_INSTALL_FULL_BINDIR}")
c6_prefix_relative(C6_LIBDIR_REL "${CMAKE_INSTALL_FULL_LIBDIR}")
c6_prefix_relative(C6_INCLUDEDIR_REL "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
c6_prefix_relative(C6_TCCDIR_REL "${C6_INSTALL_TCCDIR}")
c6_prefix_relative(C6_DOCDIR_REL "${C6_INSTALL_DOCDIR}")
c6_prefix_relative(C6_C6_DOCDIR_REL "${C6_INSTALL_C6_DOCDIR}")
c6_prefix_relative(C6_MANDIR_REL "${CMAKE_INSTALL_FULL_MANDIR}")
c6_prefix_relative(C6_INFODIR_REL "${CMAKE_INSTALL_FULL_INFODIR}")
c6_prefix_relative(C6_PKGCONFIG_DIR_REL "${C6_PKGCONFIG_DIR}")
