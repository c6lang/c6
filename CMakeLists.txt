# c6 —— 带语言扩展的 TinyCC
#
# CMake 构建入口。tcc/ 保持上游的 configure + Makefile，一行都不改：这里只从
# c6 侧把参数传进去（configure 的安装目录与开关、make 的 EXTRA-DEFS / CFLAGS /
# VAR=...），再按同一套安装目录生成 CMake / pkgconf 的库描述文件。
# tcc/ 里唯一新增的是 configure 自己生成的 config.mak、config.h 以及构建产物，
# 它们本来就在 tcc/.gitignore 里，源码与构建脚本保持原样。
#
#   cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
#   cmake --build build --parallel
#   ctest --test-dir build --output-on-failure
#   cmake --build build --target check-all      # 完整回归矩阵
#   cpack --config build/CPackConfig.cmake -G DEB -B packages
#   DESTDIR="$PWD/pkg" cmake --install build
#
# CMake 是仓库唯一的构建入口。根 Makefile 不再是构建系统，它只是回归矩阵的
# 定义（130+ 个 std-*、自举、freestanding 用例）：第 7 节会自动为其中每个
# target 生成同名 CMake custom target，并由 CMake 先构建好 c6 再调用。
# scripts/ 里的 wasm 脚本由 CMake 直接调用；bootstrap.sh 的功能已并入
# 第 2 节的 configure 阶段。

# 3.20：文档与 CI 统一使用 `ctest --test-dir build`（该选项在 3.20 才加入）。
cmake_minimum_required(VERSION 3.20)
project(c6 LANGUAGES C)

if(NOT UNIX)
  message(FATAL_ERROR
    "c6 的 CMake 包装层只对应 tcc/Makefile 的 install-unx 路径，"
    "当前平台请继续使用原来的 Makefile。")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(ProcessorCount)
include(CTest)

# ---------------------------------------------------------------------------
# 1. 从 c6 侧传入的参数（都不需要改 tcc/）
# ---------------------------------------------------------------------------

set(C6_TCC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tcc" CACHE PATH
    "tcc 源码树；只在其中生成 configure 结果与构建产物")
option(C6_ENABLE_EXT "给 tcc/Makefile 传 EXTRA-DEFS=-DC6_EXT" ON)
set(C6_EXTRA_DEFS "" CACHE STRING
    "追加到 EXTRA-DEFS 的宏，空格分隔，例如 -DC6_EXT -DFOO")
set(C6_TCC_CFLAGS "-g -O0 -Wall -Wno-unused-result" CACHE STRING
    "传给 tcc/Makefile 的 CFLAGS")
set(C6_TCC_CC "" CACHE STRING
    "tcc/configure 的 --cc；留空则用 CMAKE_C_COMPILER")
set(C6_TCC_CONFIGURE_ARGS "" CACHE STRING
    "追加给 tcc/configure 的参数，CMake 列表（分号分隔），最后传入可覆盖默认值")
set(C6_TCC_MAKE_ARGS "" CACHE STRING
    "追加给 tcc/Makefile 的 VAR=值，CMake 列表（分号分隔）")
option(C6_TCC_SHARED_LIBTCC "configure --disable-static，产出 libtcc.so" OFF)
option(C6_INSTALL_CMAKE_PACKAGE "安装 c6Config.cmake / c6Targets.cmake" ON)
option(C6_INSTALL_PKGCONF "安装 c6.pc" ON)
option(C6_INSTALL_LIBTCC_PC "额外安装 libtcc.pc 兼容名" ON)
option(C6_INSTALL_TCC_SYMLINK "安装 bindir/tcc -> c6 兼容软链" ON)
set(C6_INSTALL_TCCDIR "" CACHE STRING
    "运行时库目录；留空 = <CMAKE_INSTALL_FULL_LIBDIR>/tcc；相对值按安装前缀解析")
set(C6_INSTALL_DOCDIR "" CACHE STRING
    "文档目录；留空 = <CMAKE_INSTALL_FULL_DATAROOTDIR>/doc/tcc；相对值按安装前缀解析")

ProcessorCount(_c6_ncpu)
if(_c6_ncpu EQUAL 0)
  set(_c6_ncpu 4)
endif()
set(C6_TCC_JOBS "${_c6_ncpu}" CACHE STRING "子 make 的 -j 并发数")

# 回归矩阵（根 Makefile）用到的变量。CMake 会把它们转发给内部测试运行器，
# 否则桥接过去的 make 会用默认值，把已经构建好的编译器重新编一遍。
set(C6_HOSTCC "gcc" CACHE STRING
    "自举 / c6selfcheck 的宿主编译器（原 HOSTCC）")
set(C6_SELFTEST "tests/run-tests.sh" CACHE STRING "自测脚本（原 SELFTEST）")
set(C6_ERRBASE "tests/run-errbase.sh" CACHE STRING "errbase 脚本（原 ERRBASE）")
set(C6_CHECK_JOBS "" CACHE STRING
    "check-all 的并发数（原 CHECK_JOBS）；留空 = C6_TCC_JOBS")
set(C6_LEGACY_MAKE_VARS "" CACHE STRING
    "额外转发给回归矩阵的 VAR=值，CMake 列表（分号分隔）")
set(C6_LLVMCC "" CACHE STRING "wasm 运行库用的 clang；留空交给脚本探测")
set(C6_LLVMAR "" CACHE STRING "wasm 运行库用的 llvm-ar；留空交给脚本探测")
set(C6_NODE "" CACHE STRING "wasm 冒烟测试用的 node；留空交给脚本探测")
set(C6_WASM_TRIPLE "" CACHE STRING
    "wasm 目标 triple；留空则用 scripts/ 脚本自己的默认值")

find_program(C6_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
if(NOT EXISTS "${C6_TCC_DIR}/configure" OR NOT EXISTS "${C6_TCC_DIR}/Makefile")
  message(FATAL_ERROR
    "找不到 tcc 源码树：${C6_TCC_DIR}/configure 或 Makefile 缺失。")
endif()

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

if(NOT C6_INSTALL_TCCDIR)
  set(C6_INSTALL_TCCDIR "${CMAKE_INSTALL_FULL_LIBDIR}/tcc")
elseif(NOT IS_ABSOLUTE "${C6_INSTALL_TCCDIR}")
  set(C6_INSTALL_TCCDIR "${CMAKE_INSTALL_PREFIX}/${C6_INSTALL_TCCDIR}")
endif()
if(NOT C6_INSTALL_DOCDIR)
  set(C6_INSTALL_DOCDIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/doc/tcc")
elseif(NOT IS_ABSOLUTE "${C6_INSTALL_DOCDIR}")
  set(C6_INSTALL_DOCDIR "${CMAKE_INSTALL_PREFIX}/${C6_INSTALL_DOCDIR}")
endif()
set(C6_PKGCONFIG_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig")

c6_prefix_relative(C6_BINDIR_REL "${CMAKE_INSTALL_FULL_BINDIR}")
c6_prefix_relative(C6_LIBDIR_REL "${CMAKE_INSTALL_FULL_LIBDIR}")
c6_prefix_relative(C6_INCLUDEDIR_REL "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
c6_prefix_relative(C6_TCCDIR_REL "${C6_INSTALL_TCCDIR}")
c6_prefix_relative(C6_DOCDIR_REL "${C6_INSTALL_DOCDIR}")
c6_prefix_relative(C6_MANDIR_REL "${CMAKE_INSTALL_FULL_MANDIR}")
c6_prefix_relative(C6_INFODIR_REL "${CMAKE_INSTALL_FULL_INFODIR}")
c6_prefix_relative(C6_PKGCONFIG_DIR_REL "${C6_PKGCONFIG_DIR}")

# ---------------------------------------------------------------------------
# 2. 配置 tcc：只在 CMake configure 阶段跑一次，参数变了才重跑
# ---------------------------------------------------------------------------
#       --source-path=. 是关键：tcc/configure 由绝对/相对路径调用时，会按
#       $0 推断「树外构建」并往源码目录里覆盖软链 Makefile。显式传 . 强制
#       树内配置（TOPSRC=$(TOP)），这也是原来 bootstrap.sh 的语义。

set(_c6_cfg_args
  "--source-path=."
  "--prefix=${CMAKE_INSTALL_PREFIX}"
  "--bindir=${CMAKE_INSTALL_FULL_BINDIR}"
  "--libdir=${CMAKE_INSTALL_FULL_LIBDIR}"
  "--tccdir=${C6_INSTALL_TCCDIR}"
  "--includedir=${CMAKE_INSTALL_FULL_INCLUDEDIR}"
  "--mandir=${CMAKE_INSTALL_FULL_MANDIR}"
  "--infodir=${CMAKE_INSTALL_FULL_INFODIR}"
  "--docdir=${C6_INSTALL_DOCDIR}"
)
if(NOT "${C6_TCC_CC}" STREQUAL "")
  list(APPEND _c6_cfg_args "--cc=${C6_TCC_CC}")
elseif(CMAKE_C_COMPILER)
  list(APPEND _c6_cfg_args "--cc=${CMAKE_C_COMPILER}")
endif()
if(CMAKE_AR)
  list(APPEND _c6_cfg_args "--ar=${CMAKE_AR}")
endif()
if(C6_TCC_SHARED_LIBTCC)
  list(APPEND _c6_cfg_args "--disable-static")
endif()
if(NOT "${C6_TCC_CONFIGURE_ARGS}" STREQUAL "")
  list(APPEND _c6_cfg_args ${C6_TCC_CONFIGURE_ARGS})
endif()

# 签名包含：configure 的时间戳（本仓库改过 configure，旧 config.mak 不认识
# C99/LLVM 目标）、参数串，以及共享的 config.mak / config.h 内容哈希。
# 哈希是关键：tcc/ 是树内构建，别人（scripts/bootstrap.sh、另一个 CMake 构建
# 目录、tcc-distclean）改写过配置也要能察觉，否则会用错误的 libtcc.a/so 安装。
set(_c6_cfg_stamp "${CMAKE_CURRENT_BINARY_DIR}/c6-tcc-configure.stamp")
file(TIMESTAMP "${C6_TCC_DIR}/configure" _c6_cfg_ts)
string(REPLACE ";" " " _c6_cfg_args_flat "${_c6_cfg_args}")
set(_c6_mak_hash "-")
set(_c6_hdr_hash "-")
if(EXISTS "${C6_TCC_DIR}/config.mak")
  file(SHA256 "${C6_TCC_DIR}/config.mak" _c6_mak_hash)
endif()
if(EXISTS "${C6_TCC_DIR}/config.h")
  file(SHA256 "${C6_TCC_DIR}/config.h" _c6_hdr_hash)
endif()
set(_c6_cfg_sig
  "${C6_TCC_DIR}|${_c6_cfg_ts}|${_c6_cfg_args_flat}|${_c6_mak_hash}|${_c6_hdr_hash}")

set(_c6_need_cfg TRUE)
if(EXISTS "${_c6_cfg_stamp}"
   AND EXISTS "${C6_TCC_DIR}/config.mak"
   AND EXISTS "${C6_TCC_DIR}/config.h")
  file(READ "${_c6_cfg_stamp}" _c6_old_sig)
  string(STRIP "${_c6_old_sig}" _c6_old_sig)
  if(_c6_old_sig STREQUAL _c6_cfg_sig)
    set(_c6_need_cfg FALSE)
  endif()
endif()

if(_c6_need_cfg)
  # 树内构建只有一个 tcc/ 目标目录：configure 参数一变（例如 --disable-static
  # 切换 libtcc.a / libtcc.so），必须先清掉旧 .o，否则共享库会拿非 -fPIC 的
  # 目标文件去链接。clean 不动 config.mak/config.h，也不动源码。
  if(EXISTS "${C6_TCC_DIR}/config.mak")
    message(STATUS "c6: 配置参数变化，清理 tcc/ 旧构建产物")
    execute_process(
      COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6_TCC_DIR}" clean
      WORKING_DIRECTORY "${C6_TCC_DIR}"
      OUTPUT_QUIET ERROR_QUIET)
  endif()
  message(STATUS "c6: 配置 tcc/configure ${_c6_cfg_args_flat}")
  execute_process(
    COMMAND "${C6_TCC_DIR}/configure" ${_c6_cfg_args}
    WORKING_DIRECTORY "${C6_TCC_DIR}"
    RESULT_VARIABLE _c6_cfg_ret
    OUTPUT_VARIABLE _c6_cfg_out
    ERROR_VARIABLE _c6_cfg_err
  )
  if(NOT _c6_cfg_ret EQUAL 0)
    message(FATAL_ERROR
      "tcc/configure 失败 (${_c6_cfg_ret})：\n${_c6_cfg_out}\n${_c6_cfg_err}")
  endif()
  file(SHA256 "${C6_TCC_DIR}/config.mak" _c6_mak_hash)
  file(SHA256 "${C6_TCC_DIR}/config.h" _c6_hdr_hash)
  set(_c6_cfg_sig
    "${C6_TCC_DIR}|${_c6_cfg_ts}|${_c6_cfg_args_flat}|${_c6_mak_hash}|${_c6_hdr_hash}")
  file(WRITE "${_c6_cfg_stamp}" "${_c6_cfg_sig}")
endif()

# 从 configure 的结果里读回真实产物名（尤其 DLLSUF / 静态还是动态），
# 后面的安装规则与库配置都以它为准，不在 c6 侧另猜一套。
set(C6_TCC_DLLSUF ".so")
set(C6_TCC_EXESUF "")
set(C6_TCC_STATIC TRUE)
set(C6_TCC_CROSS FALSE)
set(C6_VERSION_RAW "0.9.28")
file(STRINGS "${C6_TCC_DIR}/config.mak" _c6_mak_lines)
foreach(_line IN LISTS _c6_mak_lines)
  if(_line MATCHES "^DLLSUF=(.*)$")
    set(C6_TCC_DLLSUF "${CMAKE_MATCH_1}")
  elseif(_line MATCHES "^EXESUF=(.*)$")
    set(C6_TCC_EXESUF "${CMAKE_MATCH_1}")
  elseif(_line MATCHES "^VERSION=(.*)$")
    string(STRIP "${CMAKE_MATCH_1}" C6_VERSION_RAW)
  elseif(_line STREQUAL "CONFIG_static=no")
    set(C6_TCC_STATIC FALSE)
  elseif(_line STREQUAL "CONFIG_cross=yes")
    set(C6_TCC_CROSS TRUE)
  endif()
endforeach()
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)+" C6_VERSION_NUMERIC "${C6_VERSION_RAW}")
if(NOT C6_VERSION_NUMERIC)
  set(C6_VERSION_NUMERIC "0.9.28")
endif()
if(NOT C6_VERSION_RAW)
  set(C6_VERSION_RAW "${C6_VERSION_NUMERIC}")
endif()

if(C6_TCC_STATIC)
  set(C6_LIBTCC_FILENAME "libtcc.a")
  set(C6_LIBTCC_KIND "STATIC")
else()
  set(C6_LIBTCC_FILENAME "libtcc${C6_TCC_DLLSUF}")
  set(C6_LIBTCC_KIND "SHARED")
endif()

# tcc/Makefile 在 unix 下给 libtcc 链的是 -lm -ldl -lpthread；静态链接的
# 消费者需要同样的库，所以写进 IMPORTED target 与 .pc 的 Libs.private。
if(APPLE)
  set(C6_INTERFACE_LINK_LIBS "m;pthread")
  set(C6_PC_PRIVATE_LIBS "-lm -lpthread")
else()
  set(C6_INTERFACE_LINK_LIBS "m;dl;pthread")
  set(C6_PC_PRIVATE_LIBS "-lm -ldl -lpthread")
endif()

# ---------------------------------------------------------------------------
# 3. 构建：等价于 make build / make build-plain
# ---------------------------------------------------------------------------
# 模式切换不必手工 clean：tcc/Makefile 会把 DEFINES/CFLAGS 记进 .c6-flags，
# 所有 .o 都依赖它，EXTRA-DEFS 一变就自动全量重编。

set(_c6_extra_defs "")
if(C6_ENABLE_EXT)
  set(_c6_extra_defs "-DC6_EXT")
endif()
if(NOT "${C6_EXTRA_DEFS}" STREQUAL "")
  if(_c6_extra_defs)
    string(APPEND _c6_extra_defs " ${C6_EXTRA_DEFS}")
  else()
    set(_c6_extra_defs "${C6_EXTRA_DEFS}")
  endif()
endif()

set(_c6_make_common
  "-C" "${C6_TCC_DIR}" "-j${C6_TCC_JOBS}" "CFLAGS=${C6_TCC_CFLAGS}")
if(NOT "${C6_TCC_MAKE_ARGS}" STREQUAL "")
  list(APPEND _c6_make_common ${C6_TCC_MAKE_ARGS})
endif()
set(_c6_make_ext "${C6_MAKE_EXECUTABLE}")
if(_c6_extra_defs)
  list(APPEND _c6_make_ext "EXTRA-DEFS=${_c6_extra_defs}")
endif()
list(APPEND _c6_make_ext ${_c6_make_common})
set(_c6_make_plain "${C6_MAKE_EXECUTABLE}" ${_c6_make_common})

add_custom_target(c6 ALL
  COMMAND ${_c6_make_ext}
  WORKING_DIRECTORY "${C6_TCC_DIR}"
  COMMENT "c6: 构建 ${C6_TCC_DIR}/c6（EXTRA-DEFS=${_c6_extra_defs}）"
  VERBATIM USES_TERMINAL)
add_custom_target(build DEPENDS c6)
add_custom_target(build-plain
  COMMAND ${_c6_make_plain}
  WORKING_DIRECTORY "${C6_TCC_DIR}"
  COMMENT "c6: 上游模式构建（不带 EXTRA-DEFS）"
  VERBATIM USES_TERMINAL)
add_custom_target(bootstrap
  COMMAND "${C6_TCC_DIR}/configure" ${_c6_cfg_args}
  WORKING_DIRECTORY "${C6_TCC_DIR}"
  COMMENT "c6: 重新运行 tcc/configure"
  VERBATIM USES_TERMINAL)
add_custom_target(tcc-clean
  COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6_TCC_DIR}" clean
  WORKING_DIRECTORY "${C6_TCC_DIR}" VERBATIM USES_TERMINAL)
add_custom_target(tcc-distclean
  COMMAND "${C6_MAKE_EXECUTABLE}" -C "${C6_TCC_DIR}" distclean
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${_c6_cfg_stamp}"
  WORKING_DIRECTORY "${C6_TCC_DIR}"
  COMMENT "c6: 清理 tcc 构建产物与 configure 结果（需重新 cmake configure）"
  VERBATIM USES_TERMINAL)

# ---------------------------------------------------------------------------
# 4. 安装：与 tcc/Makefile 的 install-unx 同一套文件，但由 CMake 自己装，
#    文件落点 / DESTDIR 按 CMake 语义生效，且与生成的库配置共用一套路径。
#    注意二进制里的 CONFIG_TCCDIR、rpath 在 configure 时已烤死，不能用
#    cmake --install --prefix 改；换前缀要重新 configure + build。
# ---------------------------------------------------------------------------

install(PROGRAMS "${C6_TCC_DIR}/c6${C6_TCC_EXESUF}"
        DESTINATION "${C6_BINDIR_REL}")
# install-unx 对共享库用 IBw（0755），静态库用 IFw（0644）
if(C6_TCC_STATIC)
  install(FILES "${C6_TCC_DIR}/${C6_LIBTCC_FILENAME}"
          DESTINATION "${C6_LIBDIR_REL}")
else()
  install(PROGRAMS "${C6_TCC_DIR}/${C6_LIBTCC_FILENAME}"
          DESTINATION "${C6_LIBDIR_REL}")
endif()
install(FILES "${C6_TCC_DIR}/libtcc.h"
        DESTINATION "${C6_INCLUDEDIR_REL}")
install(FILES "${C6_TCC_DIR}/libtcc1.a"
        DESTINATION "${C6_TCCDIR_REL}")
# 这几个 .o 是否存在取决于 CONFIG_bcheck / CONFIG_backtrace（bt-dll.o 只在
# win32 下产出），逐个 OPTIONAL；libtcc1.a 则必须存在。
foreach(_o IN ITEMS runmain.o bt-exe.o bt-dll.o bt-log.o bcheck.o bcheck_run.o)
  install(FILES "${C6_TCC_DIR}/${_o}" DESTINATION "${C6_TCCDIR_REL}" OPTIONAL)
endforeach()
file(GLOB _c6_tcc_headers "${C6_TCC_DIR}/include/*.h")
install(FILES ${_c6_tcc_headers} "${C6_TCC_DIR}/tcclib.h"
        DESTINATION "${C6_TCCDIR_REL}/include")
install(FILES "${C6_TCC_DIR}/tcc.1"
        DESTINATION "${C6_MANDIR_REL}/man1" OPTIONAL)
install(FILES "${C6_TCC_DIR}/tcc-doc.info"
        DESTINATION "${C6_INFODIR_REL}" OPTIONAL)
install(FILES "${C6_TCC_DIR}/tcc-doc.html"
        DESTINATION "${C6_DOCDIR_REL}" OPTIONAL)

# 交叉产物是构建期生成的，安装时才枚举（见 cmake/c6-install-cross.cmake.in）。
if(C6_TCC_CROSS)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6-install-cross.cmake.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/c6-install-cross.cmake" @ONLY)
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/c6-install-cross.cmake")
endif()

if(C6_INSTALL_TCC_SYMLINK)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6-post-install.cmake.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/c6-post-install.cmake" @ONLY)
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/c6-post-install.cmake")
endif()

# ---------------------------------------------------------------------------
# 5. CMake 库配置：find_package(c6) -> c6::libtcc / c6::compiler
# ---------------------------------------------------------------------------

if(C6_INSTALL_CMAKE_PACKAGE)
  set(C6_PACKAGE_DIR "${C6_LIBDIR_REL}/cmake/c6")
  set(C6_BINDIR "${CMAKE_INSTALL_FULL_BINDIR}")
  set(C6_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
  set(C6_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
  set(C6_TCCDIR "${C6_INSTALL_TCCDIR}")

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/c6Config.cmake"
    INSTALL_DESTINATION "${C6_PACKAGE_DIR}"
    PATH_VARS C6_BINDIR C6_LIBDIR C6_INCLUDEDIR C6_TCCDIR
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/c6ConfigVersion.cmake"
    VERSION "${C6_VERSION_NUMERIC}"
    COMPATIBILITY SameMajorVersion
  )
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6Targets.cmake.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/c6Targets.cmake" @ONLY)
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/c6Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/c6ConfigVersion.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/c6Targets.cmake"
    DESTINATION "${C6_PACKAGE_DIR}")
endif()

# ---------------------------------------------------------------------------
# 6. pkgconf 库配置：pkgconf --cflags --libs c6 / libtcc
# ---------------------------------------------------------------------------

if(C6_INSTALL_PKGCONF)
  # prefix 用 ${pcfiledir} 反推，安装到别的前缀/DESTDIR 里也能直接用。
  file(RELATIVE_PATH C6_PC_PREFIX_REL "${C6_PKGCONFIG_DIR}" "${CMAKE_INSTALL_PREFIX}")
  string(REGEX REPLACE "/+$" "" C6_PC_PREFIX_REL "${C6_PC_PREFIX_REL}")
  if(C6_PC_PREFIX_REL STREQUAL "" OR C6_PC_PREFIX_REL STREQUAL ".")
    set(C6_PC_PREFIX "\${pcfiledir}")
  else()
    set(C6_PC_PREFIX "\${pcfiledir}/${C6_PC_PREFIX_REL}")
  endif()

  set(C6_PC_NAME "c6")
  set(C6_PC_DESCRIPTION "c6 - TinyCC with language extensions (libtcc embedding API)")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6.pc.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/c6.pc" @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/c6.pc"
          DESTINATION "${C6_PKGCONFIG_DIR_REL}")

  if(C6_INSTALL_LIBTCC_PC)
    set(C6_PC_NAME "libtcc")
    set(C6_PC_DESCRIPTION "libtcc - TinyCC/c6 embeddable C compiler library")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6.pc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/libtcc.pc" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libtcc.pc"
            DESTINATION "${C6_PKGCONFIG_DIR_REL}")
  endif()
endif()

# ---------------------------------------------------------------------------
# 7. 测试与回归矩阵：CTest 是日常入口，Makefile 只作为用例定义
# ---------------------------------------------------------------------------

if(BUILD_TESTING)
  set(_c6_test_env
    "TCC=${C6_TCC_DIR}/c6${C6_TCC_EXESUF}"
    "C99TCC=${C6_TCC_DIR}/c6${C6_TCC_EXESUF}")
  if(NOT C6_TCC_STATIC)
    set(_c6_ld_path "${C6_TCC_DIR}:$ENV{LD_LIBRARY_PATH}")
    string(REGEX REPLACE ":+$" "" _c6_ld_path "${_c6_ld_path}")
    list(APPEND _c6_test_env "LD_LIBRARY_PATH=${_c6_ld_path}")
  endif()

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
    ENVIRONMENT "${_c6_test_env}"
    TIMEOUT 1800)
  set_tests_properties(c6-c99-selftest PROPERTIES
    ENVIRONMENT "${_c6_test_env};BACKEND=c99")
  set(_c6_c99tcc_env
    "${_c6_test_env};BACKEND=c99;C99CC=${C6_TCC_DIR}/c6${C6_TCC_EXESUF};C99FLAGS=-B ${C6_TCC_DIR} -std=c99 -w")
  set_tests_properties(c6-c99tcc-selftest PROPERTIES
    ENVIRONMENT "${_c6_c99tcc_env}")
endif()

# 根 Makefile 的 130+ 个 target（91 个 std-*、9 个 fstest*、自举、C99
# 后端、安全门等）全部由 CMake 暴露成同名 target。CMake 先把编译器构建好
# （add_dependencies(... c6)），再让 make 只负责跑用例：-o build 等跳过 make
# 自己的构建依赖（Makefile 的 check-all 也是这么做的），否则它会按默认参数
# 重编一遍，覆盖掉 CMake 传进去的 CFLAGS / EXTRA-DEFS / configure 结果。
set(_c6_check_jobs "${C6_CHECK_JOBS}")
if("${_c6_check_jobs}" STREQUAL "")
  set(_c6_check_jobs "${C6_TCC_JOBS}")
endif()
set(_c6_matrix_vars
  "SELFTEST=${C6_SELFTEST}"
  "ERRBASE=${C6_ERRBASE}"
  "HOSTCC=${C6_HOSTCC}"
  "JOBS=${C6_TCC_JOBS}"
  "CFLAGS=${C6_TCC_CFLAGS}"
  "CHECK_JOBS=${_c6_check_jobs}"
)
# check-all / check-std-serial 的递归 make 会清空 MAKEFLAGS，顶层 make 的
# 命令行变量到不了那一层。所以同一份设置也放进环境：Makefile 里这些变量用
# ?= 定义，递归层会从环境继承（命令行变量仍然优先）。
set(_c6_matrix_env "${CMAKE_COMMAND}" -E env
  "SELFTEST=${C6_SELFTEST}"
  "ERRBASE=${C6_ERRBASE}"
  "HOSTCC=${C6_HOSTCC}"
  "JOBS=${C6_TCC_JOBS}"
  "CFLAGS=${C6_TCC_CFLAGS}"
  "CHECK_JOBS=${_c6_check_jobs}"
)
if(NOT "${C6_LLVMCC}" STREQUAL "")
  list(APPEND _c6_matrix_env "LLVMCC=${C6_LLVMCC}")
  list(APPEND _c6_matrix_vars "LLVMCC=${C6_LLVMCC}")
endif()
if(NOT "${C6_WASM_TRIPLE}" STREQUAL "")
  list(APPEND _c6_matrix_env "WASM_TRIPLE=${C6_WASM_TRIPLE}")
  list(APPEND _c6_matrix_vars "WASM_TRIPLE=${C6_WASM_TRIPLE}")
endif()
if(NOT "${C6_LEGACY_MAKE_VARS}" STREQUAL "")
  list(APPEND _c6_matrix_vars ${C6_LEGACY_MAKE_VARS})
endif()

# configure 时扫描 Makefile：以后往回归矩阵里加/删用例，CMake 目标自动同步，
# 不必再维护一份 130 行的清单。Makefile 在这里只是「用例定义」，不是构建入口。
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/Makefile" _c6_mk_lines
     REGEX "^[A-Za-z0-9_.-]+:")
set(_c6_matrix_targets "")
foreach(_line IN LISTS _c6_mk_lines)
  string(REGEX MATCH "^([A-Za-z0-9_.-]+):" _unused "${_line}")
  if(NOT CMAKE_MATCH_1 STREQUAL ".PHONY")
    list(APPEND _c6_matrix_targets "${CMAKE_MATCH_1}")
  endif()
endforeach()
list(REMOVE_DUPLICATES _c6_matrix_targets)
list(SORT _c6_matrix_targets)

# 这些名字 CMake 已有（或语义变了），不能重复 add_custom_target。
# all/test/clean/help 分别是 CMake 的 ALL、CTest、clean、help 内置目标；
# build/build-plain/bootstrap/c6 在第 3 节原生实现；wasm 两个目标直调脚本；
# test-plain 要先切到上游模式，单独实现。
set(_c6_native_targets
  all test clean help
  build build-plain bootstrap c6
  mode-ext mode-plain distclean wasmrt wasmtest test-plain
)

set(_c6_bridge_count 0)
foreach(_t IN LISTS _c6_matrix_targets)
  if(NOT _t IN_LIST _c6_native_targets)
    add_custom_target("${_t}"
      COMMAND ${_c6_matrix_env} "${C6_MAKE_EXECUTABLE}"
              -C "${CMAKE_CURRENT_SOURCE_DIR}" --no-print-directory
              -o build -o build-plain -o mode-ext -o mode-plain -o bootstrap
              "${_t}" ${_c6_matrix_vars}
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      COMMENT "c6: 回归矩阵 ${_t}（CMake 已构建好编译器，Makefile 只跑用例）"
      VERBATIM USES_TERMINAL)
    # 目标级依赖（不是 DEPENDS 的文件级依赖）：换生成器也保证先编译 c6。
    add_dependencies("${_t}" c6)
    math(EXPR _c6_bridge_count "${_c6_bridge_count} + 1")
  endif()
endforeach()

# test-plain 的语义是「上游模式重建后再跑 tests2」。只用 -o 跳过构建会让它
# 拿扩展版编译器跑，所以显式先构建 build-plain，再让 make 只跑测试。
add_custom_target(test-plain
  COMMAND ${_c6_matrix_env} "${C6_MAKE_EXECUTABLE}"
          -C "${CMAKE_CURRENT_SOURCE_DIR}" --no-print-directory
          -o build -o build-plain -o mode-ext -o mode-plain -o bootstrap
          test-plain ${_c6_matrix_vars}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "c6: 上游模式重建后跑 tests2 全量"
  VERBATIM USES_TERMINAL)
add_dependencies(test-plain build-plain)
math(EXPR _c6_bridge_count "${_c6_bridge_count} + 1")

# `test` 这个名字被 CTest 保留（即使 BUILD_TESTING=OFF 也不能注册同名
# custom target）：`cmake --build build --target test` / `ctest` 跑的是上面
# 5 个 CTest 用例，其中 c6-tests2 就是原来的 tests2 全量测试；只想跑那一
# 条用 `ctest --test-dir build -R c6-tests2`。

# 模式切换不再靠 make 往 tcc/ 写 .c6-mode 再 clean：模式是 CMake 的配置项。
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

# wasm：CMake 直接调用 scripts/，不再绕道根 Makefile。LLVMCC / LLVMAR /
# NODE / WASM_TRIPLE 都从 -D 参数注入脚本的环境。
set(_c6_wasm_env "${CMAKE_COMMAND}" -E env)
if(NOT "${C6_LLVMCC}" STREQUAL "")
  list(APPEND _c6_wasm_env "LLVMCC=${C6_LLVMCC}")
endif()
if(NOT "${C6_LLVMAR}" STREQUAL "")
  list(APPEND _c6_wasm_env "LLVMAR=${C6_LLVMAR}")
endif()
if(NOT "${C6_NODE}" STREQUAL "")
  list(APPEND _c6_wasm_env "NODE=${C6_NODE}")
endif()
if(NOT C6_TCC_STATIC)
  set(_c6_wasm_ld "${C6_TCC_DIR}:$ENV{LD_LIBRARY_PATH}")
  string(REGEX REPLACE ":+$" "" _c6_wasm_ld "${_c6_wasm_ld}")
  list(APPEND _c6_wasm_env "LD_LIBRARY_PATH=${_c6_wasm_ld}")
endif()
if(NOT "${C6_WASM_TRIPLE}" STREQUAL "")
  set(_c6_wasm_args "${C6_WASM_TRIPLE}")
else()
  set(_c6_wasm_args "")
endif()

add_custom_target(wasmrt
  COMMAND ${_c6_wasm_env}
          "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-wasm-runtime.sh"
          ${_c6_wasm_args}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "c6: 编 wasm 交叉运行库 tcc/libtcc1-<arch>.a"
  VERBATIM USES_TERMINAL)
add_dependencies(wasmrt c6)
add_custom_target(wasmtest
  COMMAND ${_c6_wasm_env}
          "${CMAKE_CURRENT_SOURCE_DIR}/scripts/wasm-smoke.sh" ${_c6_wasm_args}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "c6: --emit-llvm -> .wasm -> node 端到端冒烟"
  VERBATIM USES_TERMINAL)
add_dependencies(wasmtest c6)

# 清理：CMake 自带的 clean 只清构建目录；这两个目标对应旧 Makefile 的
# clean / distclean（tcc/ 是树内构建，产物都在源码树里）。
add_custom_target(c6-clean
  COMMAND "${C6_MAKE_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" clean
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "c6: 清理自举/裸机产物与 tcc/ 目标文件（等价 Makefile clean）"
  VERBATIM USES_TERMINAL)
add_custom_target(clean-all
  COMMAND "${C6_MAKE_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" distclean
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${_c6_cfg_stamp}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "c6: 连 configure 结果一起清理（之后需重新 cmake -S . -B build）"
  VERBATIM USES_TERMINAL)
add_custom_target(distclean)
add_dependencies(distclean clean-all)

# help 是 CMake 内置目标（`--target help` 列出全部 CMake target），
# c6-help 额外打印日常用法，再附上 Makefile 里的用例清单。
add_custom_target(c6-help
  COMMAND "${CMAKE_COMMAND}" -E echo "c6: CMake 是唯一构建入口"
  COMMAND "${CMAKE_COMMAND}" -E echo "  配置    cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr"
  COMMAND "${CMAKE_COMMAND}" -E echo "  构建    cmake --build build --parallel"
  COMMAND "${CMAKE_COMMAND}" -E echo "  测试    ctest --test-dir build --output-on-failure"
  COMMAND "${CMAKE_COMMAND}" -E echo "  完整回归 cmake --build build --target check-all"
  COMMAND "${CMAKE_COMMAND}" -E echo "  安装    DESTDIR=\$PWD/pkg cmake --install build"
  COMMAND "${CMAKE_COMMAND}" -E echo "  打包    cpack --config build/CPackConfig.cmake -G DEB -B packages"
  COMMAND "${CMAKE_COMMAND}" -E echo "  全部目标 cmake --build build --target help"
  COMMAND "${C6_MAKE_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" help
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  VERBATIM USES_TERMINAL)

# check-install：把 install() 结果装进 DESTDIR 暂存树，再用它配置、编译、
# 运行一个 find_package(c6) 的消费方，并校验 pkgconf 元数据。CI 与本地都用
# 这一条验证「装出去的库配置真的能用」。
set(C6_INSTALL_PREFIX_REL "${CMAKE_INSTALL_PREFIX}")
string(REGEX REPLACE "^/+" "" C6_INSTALL_PREFIX_REL "${C6_INSTALL_PREFIX_REL}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6-check-install.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/c6-check-install.cmake" @ONLY)
add_custom_target(check-install
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/c6-check-install.cmake"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  COMMENT "c6: 校验 DESTDIR 安装树里的 CMake / pkgconf 库配置"
  VERBATIM USES_TERMINAL)
add_dependencies(check-install c6)

message(STATUS
  "c6 ${C6_VERSION_RAW}: ext=${C6_ENABLE_EXT}, ${C6_LIBTCC_FILENAME}, "
  "prefix=${CMAKE_INSTALL_PREFIX}, tccdir=${C6_INSTALL_TCCDIR}, "
  "回归矩阵 ${_c6_bridge_count} 个同名 target")

# ---------------------------------------------------------------------------
# 8. 打包：TGZ / DEB / RPM，复用上面同一套 install() 清单
# ---------------------------------------------------------------------------

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPack.cmake")
