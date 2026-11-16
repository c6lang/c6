# c6c —— 用 c6 语言写的自举编译器（native x86-64 / aarch64 / C99 / LLVM IR 后端）
#
# **c6c 没有自己的构建系统**：编 c6c、跑它的全部验收、装它的库与元数据，都是
# 这一组 CMake 模块定义的 target。`c6c/Makefile` 已经删掉，`make -C c6c ...`
# 那条路不再存在。
#
# 本文件只是总控：定路径、发现工具、算出各子模块共用的环境，然后按顺序 include
# 四个子模块。要改哪一块就只看那一块，不用在 400 行的文件里翻。
#
#   C6CBuild     编 c6c / c6c-opt / libc6c（stage0 两条路）
#   C6CCheck     验收 target（unit / 各后端 check / 语料 / 自举 / 跑分 / g2c）
#   C6CGate      聚合门禁（fast / check-all-c6c / check-all-perf）+ CTest 注册
#   C6CInstall   嵌入库与头文件安装、find_package(c6c) 与 c6c.pc
#
# 常用入口（`-S . -B build` 之后）：
#
#   cmake --build build                       默认目标：c6 产品 + c6c + libc6c
#   cmake --build build --target c6c          只编 c6c（`c6c/build/c6c`）
#   cmake --build build --target c6c-lib      只编嵌入库 libc6c.a / .so
#   cmake --build build --target fast         c6c 快速门禁（≈40 s，日常改一行）
#   cmake --build build --target check-all-c6c  全量验收（≈4 min，提交/发布前）
#   cmake --build build --target help         CMake 自己会列出全部 target
#   ctest -L c6c                              CTest 视角的同一套验收
#
# 每个 CMake target 对应的就是原来 c6c/Makefile 里的同名目标（逐条映射见
# `c6c/docs/acceptance.md` 的表格；只有两个改了名字：`test` → `test-c6c`、
# `ctest` → `ctest-c6c`，因为 `test` 是 CMake 内建 target）。
#
# 引导编译器（stage0）：
#   C6_ENABLE_TCC=ON（默认）→ 仓库自己的 tcc/c6，`-B tcc` 带 crt / libtcc1.a；
#   C6_ENABLE_TCC=OFF        → 宿主 C 编译器，`-x c` 直接编 c6c 的单 TU 源码。
# 两条路产物同一份源码、同一套语义；OFF 这条路**不需要任何 tcc 构建产物**。

set(C6C_DIR "${CMAKE_CURRENT_SOURCE_DIR}/c6c")
set(C6C_BUILD "${C6C_DIR}/build")
set(C6C_BIN "${C6C_BUILD}/c6c")
set(C6C_OPT "${C6C_BUILD}/c6c-opt")
set(C6C_LIB_A "${C6C_BUILD}/libc6c.a")
set(C6C_LIB_SO "${C6C_BUILD}/libc6c.so")
set(C6C_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# bench.py 是 Python 脚本：显式用解释器调用，不依赖文件可执行位
# （git 里它是 100644，AUR check 直接 exec 会得到 permission denied）。
# Python3 是 c6c 跑分/回归的依赖，详见 README 与 docs/manual/testing.md。
find_package(Python3 COMPONENTS Interpreter REQUIRED)

include(ProcessorCount)
ProcessorCount(C6C_JOBS)
if(C6C_JOBS EQUAL 0)
  set(C6C_JOBS 4)
endif()

# 引导编译器 tcc/c6 在 shared 形态（-DC6_TCC_SHARED_LIBTCC=ON）下自己就链到
# libtcc.so：构建树里的 c6 没有 rpath，直接执行会报 libtcc.so 找不到。所以凡是
# 要**执行** c6 的地方都补 LD_LIBRARY_PATH，口径与 C6Testing.cmake 一致。
set(_c6c_ld_env "")
if(C6_ENABLE_TCC AND NOT C6_TCC_STATIC)
  set(_c6c_ld_path "${C6_TCC_DIR}:$ENV{LD_LIBRARY_PATH}")
  string(REGEX REPLACE ":+$" "" _c6c_ld_path "${_c6c_ld_path}")
  set(_c6c_ld_env "LD_LIBRARY_PATH=${_c6c_ld_path}")
endif()

file(GLOB C6C_SRCS CONFIGURE_DEPENDS "${C6C_DIR}/src/*.c6")

# llc 常常装在 /usr/lib/llvm-NN/bin 而不是 PATH 里：先 PATH，再挑一个装好的版本
find_program(C6C_LLC NAMES llc)
if(NOT C6C_LLC)
  file(GLOB _c6c_llcs "/usr/lib/llvm-*/bin/llc")
  if(_c6c_llcs)
    list(SORT _c6c_llcs COMPARE NATURAL)
    list(GET _c6c_llcs -1 C6C_LLC)
  endif()
endif()

include("${C6_MODULE_DIR}/C6CBuild.cmake")
include("${C6_MODULE_DIR}/C6CCheck.cmake")
include("${C6_MODULE_DIR}/C6CGate.cmake")
include("${C6_MODULE_DIR}/C6CInstall.cmake")
