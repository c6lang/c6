# c6 —— 带语言扩展的 TinyCC
#
# CMake 是仓库唯一的构建入口。tcc/CMakeLists.txt 按上游 configure 的规则生成
# config.mak / config.h / config.texi（不再运行 configure 脚本），编译仍由上游
# tcc/Makefile 完成；configure 与 Makefile 源码一行不改，参数都从 c6 侧传入。
#
# 本文件只做三件事：加载 CMake 内置模块、按顺序 include 各阶段模块、打印汇总。
#   C6Options          参数与安装路径
#   C6Tcc              add_subdirectory(tcc)，传参数并读回 tcc 导出的产物信息
#   C6Build            构建目标（薄封装 tcc-build）
#   C6Install          install() 清单
#   C6LibraryConfig    find_package(c6) / pkgconf 库配置
#   C6Wasm             wasm 目标（直调 scripts/）
#   C6UtilityTargets   清理 / c6-help
#   C6CheckInstall     DESTDIR 安装树校验
#   C6Testing          原生 CTest：日常用例 + 完整回归矩阵
#   C6Packaging        TGZ / DEB / RPM 打包
#
# 根 Makefile 已移除：回归用例的实现在 tests/regression.sh（纯 shell），
# 调度由 CTest 负责。scripts/bootstrap.sh 已移除，功能由 tcc/CMakeLists.txt 取代。
#
#   cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
#   cmake --build build --parallel
#   ctest --test-dir build -L daily --output-on-failure
#   ctest --test-dir build -L regression -j 8 --output-on-failure
#   cpack --config build/CPackConfig.cmake -G DEB -B packages
#   DESTDIR="$PWD/pkg" cmake --install build

cmake_minimum_required(VERSION 3.20)

# 版本以 tcc/VERSION 为唯一来源；CMake 的 project(VERSION) 只接受数值段，
# rc 之类的预发布后缀仍由 C6_VERSION_RAW 保留给打包用。
file(READ "${CMAKE_CURRENT_LIST_DIR}/tcc/VERSION" _c6_version_raw)
string(STRIP "${_c6_version_raw}" _c6_version_raw)
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)+" _c6_version_numeric "${_c6_version_raw}")
project(c6
  VERSION "${_c6_version_numeric}"
  DESCRIPTION "c6 - TinyCC with language extensions"
  HOMEPAGE_URL "https://github.com/c6lang/c6"
  LANGUAGES C)

if(NOT UNIX)
  message(FATAL_ERROR
    "c6 的 CMake 包装层只对应 tcc/Makefile 的 install-unx 路径。")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(ProcessorCount)
include(CTest)

set(C6_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake")
include("${C6_MODULE_DIR}/C6Options.cmake")
include("${C6_MODULE_DIR}/C6Tcc.cmake")
include("${C6_MODULE_DIR}/C6Build.cmake")
include("${C6_MODULE_DIR}/C6Install.cmake")
include("${C6_MODULE_DIR}/C6LibraryConfig.cmake")
include("${C6_MODULE_DIR}/C6Wasm.cmake")
include("${C6_MODULE_DIR}/C6UtilityTargets.cmake")
include("${C6_MODULE_DIR}/C6CheckInstall.cmake")
include("${C6_MODULE_DIR}/C6Testing.cmake")

math(EXPR _c6_gate_count
     "${C6_REGRESSION_CASE_COUNT} - ${C6_AUX_CASE_COUNT}")
message(STATUS
  "c6 ${C6_VERSION_RAW}: ext=${C6_ENABLE_EXT}, ${C6_LIBTCC_FILENAME}, "
  "prefix=${CMAKE_INSTALL_PREFIX}, tccdir=${C6_INSTALL_TCCDIR}, "
  "CTest 用例 ${C6_REGRESSION_CASE_COUNT} 个"
  "（门禁 ${_c6_gate_count} / aux ${C6_AUX_CASE_COUNT}）")

# CPack 必须在所有 install() 规则之后加载；模块名不与内置 CPack 冲突。
include("${C6_MODULE_DIR}/C6Packaging.cmake")
