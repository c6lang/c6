# 直接使用 tcc/ 自带的 CMake 配置。
#
# tcc/CMakeLists.txt 按上游 tcc/configure 的规则生成 config.mak / config.h /
# config.texi，并定义 tcc-build / tcc-build-plain / tcc-config 等目标；这里只把
# c6 侧的参数传下去，再从 tcc::c6 / tcc::libtcc 读回产物信息。不再运行
# tcc/configure，也不需要签名戳：配置同步与清理由 CMake 的构建依赖驱动。

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

set(TCC_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(TCC_INSTALL_BINDIR "${CMAKE_INSTALL_FULL_BINDIR}")
set(TCC_INSTALL_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
set(TCC_INSTALL_TCCDIR "${C6_INSTALL_TCCDIR}")
set(TCC_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
set(TCC_INSTALL_MANDIR "${CMAKE_INSTALL_FULL_MANDIR}")
set(TCC_INSTALL_INFODIR "${CMAKE_INSTALL_FULL_INFODIR}")
set(TCC_INSTALL_DOCDIR "${C6_INSTALL_DOCDIR}")
if(NOT "${C6_TCC_CC}" STREQUAL "")
  set(TCC_CC "${C6_TCC_CC}")
endif()
set(TCC_AR "${CMAKE_AR}")
set(TCC_MAKE_EXECUTABLE "${C6_MAKE_EXECUTABLE}")
set(TCC_JOBS "${C6_TCC_JOBS}")
set(TCC_EXTRA_DEFS "${_c6_extra_defs}")
set(TCC_BUILD_CFLAGS "${C6_TCC_CFLAGS}")
set(TCC_SHARED_LIBTCC "${C6_TCC_SHARED_LIBTCC}")
if(NOT "${C6_TCC_MAKE_ARGS}" STREQUAL "")
  set(TCC_MAKE_ARGS "${C6_TCC_MAKE_ARGS}")
endif()
if(NOT "${C6_TCC_CONFIG_VARS}" STREQUAL "")
  set(TCC_EXTRA_CONFIG_VARS "${C6_TCC_CONFIG_VARS}")
endif()

# c6 自己的发布版本（根目录 VERSION）传给 tcc 侧，由 config.h 的 C6_VERSION
# 宏带进编译期，`c6 -v` 打印的就是它；tcc/VERSION 仍只当上游兼容版本。
set(C6_RELEASE_VERSION "${C6_VERSION_RAW}")

add_subdirectory("${C6_TCC_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/tcc")

# 产物路径只通过 tcc::c6 / tcc::libtcc 暴露；这里只把父项目后续模块仍要
# 用到的少量语义变量改名成 C6_*，不再在父子作用域之间散落 TCC_* 全局量。
# C6_VERSION_RAW 不在这里赋值：它由顶层 CMakeLists.txt 按根目录 VERSION 设好，
# 是 c6 的发布版本；tcc/VERSION 读回的 TCC_VERSION 只能当兼容版本用。

set(C6_TCC_VERSION "${TCC_VERSION}")
set(C6_TCC_VERSION_NUMERIC "${TCC_VERSION_NUMERIC}")
set(C6_VERSION_NUMERIC "${PROJECT_VERSION}")
set(C6_LIBTCC_FILENAME "${TCC_LIBTCC_FILENAME}")
set(C6_LIBTCC_KIND "${TCC_LIBTCC_KIND}")
set(C6_TCC_STATIC "${TCC_STATIC}")
set(C6_TCC_CROSS "${TCC_CROSS}")
set(C6_TCC_DLLSUF "${TCC_DLLSUF}")
set(C6_TCC_EXESUF "${TCC_EXESUF}")
set(C6_INTERFACE_LINK_LIBS "${TCC_INTERFACE_LINK_LIBS}")
set(C6_PC_PRIVATE_LIBS "${TCC_PC_PRIVATE_LIBS}")
