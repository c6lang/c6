# c6c 的安装：嵌入库 / 头文件，以及它**自己的** CMake 包配置与 pkgconf 元数据。
#
# 由 cmake/C6C.cmake 顺序 include；依赖 C6Options.cmake 里的安装路径变量
# （C6_BINDIR_REL / C6_LIBDIR_REL / C6_INCLUDEDIR_REL / C6_PKGCONFIG_DIR）。
#
# ---------------------------------------------------------------------------
# 为什么 c6c 有自己的 find_package(c6c) / pkgconf c6c，而不是全塞进 c6：
#
#   * `c6` 与 `c6c` 是两个产品：`c6` 是 TinyCC 分支（libtcc、LGPL），`c6c` 是用
#     c6 语言自举重写的编译器 + 嵌入库 libc6c（c6_* API，MIT）。下游写
#     `find_package(c6c)` / `pkgconf --libs c6c` 才是它真正依赖的东西，名字对不上
#     就只能靠"c6 其实是 c6c"这种口耳相传的知识。
#   * pkgconf 的包名是**消费者源码里写死**的：`c6.pc` 在两种 flavour 下内容不同
#     （有 tcc 时 -ltcc、没有时 -lc6c）。让 c6c 包只提供 c6.pc，等于两个互斥的包
#     抢同一个名字；c6c.pc 是**无歧义**的那一份。
#   * 兼容面照旧：`c6.pc` / `c6lib.pc` / `find_package(c6)` 仍然照装（见
#     C6LibraryConfig.cmake），老消费者不用改。
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# 1. 嵌入库 libc6c 与宿主头文件
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# 2. find_package(c6c)：c6c::compiler / c6c::libc6c
#
# 只有"这次会装 c6c"（默认与 tcc 并存，或 C6_ENABLE_TCC=OFF 时它就是 c6）才生成
# 配置——不装二进制却给一份指向空路径的包配置，比没有更糟。
# ---------------------------------------------------------------------------
set(_c6c_installed FALSE)
if(C6_INSTALL_C6C OR NOT C6_ENABLE_TCC)
  set(_c6c_installed TRUE)
endif()

if(_c6c_installed AND C6_INSTALL_CMAKE_PACKAGE)
  set(C6C_PACKAGE_DIR "${C6_LIBDIR_REL}/cmake/c6c")
  set(C6C_BINDIR "${CMAKE_INSTALL_FULL_BINDIR}")
  set(C6C_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
  set(C6C_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6cConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/c6cConfig.cmake"
    INSTALL_DESTINATION "${C6C_PACKAGE_DIR}"
    PATH_VARS C6C_BINDIR C6C_LIBDIR C6C_INCLUDEDIR
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/c6cConfigVersion.cmake"
    VERSION "${C6_VERSION_NUMERIC}"
    COMPATIBILITY SameMajorVersion
  )
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6cTargets.cmake.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/c6cTargets.cmake" @ONLY)
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/c6cConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/c6cConfigVersion.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/c6cTargets.cmake"
    DESTINATION "${C6C_PACKAGE_DIR}")
endif()

# ---------------------------------------------------------------------------
# 3. pkgconf c6c：pkgconf --cflags --libs c6c -> -lc6c（宿主侧头文件在 includedir）
#
# 只描述**装进 includedir 的宿主头**（c6lib.h / libtcc.h）；c6c 自己那套 stdio.h
# 在 <libdir>/c6/include，不能进 Cflags，否则会盖掉宿主程序的系统头。
# ---------------------------------------------------------------------------
if(_c6c_installed AND C6_INSTALL_PKGCONF)
  c6_pcfile_prefix(C6C_PC_PREFIX "${C6_PKGCONFIG_DIR}")
  set(C6C_PC_NAME "c6c")
  set(C6C_PC_DESCRIPTION
      "c6c - self-hosted compiler written in c6 (embeddable libc6c)")
  set(C6C_PC_LIBS "-lc6c")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6c.pc.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/c6c.pc" @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/c6c.pc"
          DESTINATION "${C6_PKGCONFIG_DIR_REL}")
endif()
