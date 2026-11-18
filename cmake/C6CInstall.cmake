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
  # c6c 的宿主头只有 c6lib.h（c6_* API）：不装 tcc 兼容头，也不会装出
  # libtcc.h 这个名字。需要 libtcc 的老消费者必须显式用有 tcc 的构建。
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
  set(C6C_BINDIR "${CMAKE_INSTALL_FULL_BINDIR}")
  set(C6C_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
  set(C6C_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
  # C6CheckInstall.cmake 的 check-install 脚本要按这个落点核对安装树
  set(C6C_PACKAGE_DIR "${C6_LIBDIR_REL}/cmake/c6c")
  c6_install_cmake_package(c6c "${C6C_PACKAGE_DIR}"
    c6cConfig.cmake.in c6cTargets.cmake.in
    C6C_BINDIR C6C_LIBDIR C6C_INCLUDEDIR)
endif()

# ---------------------------------------------------------------------------
# 3. pkgconf c6c：pkgconf --cflags --libs c6c -> -lc6c（宿主侧头文件在 includedir）
#
# 只描述**装进 includedir 的宿主头**（c6lib.h）；c6c 自己那套 stdio.h
# 在 <libdir>/c6/include，不能进 Cflags，否则会盖掉宿主程序的系统头。
# ---------------------------------------------------------------------------
if(_c6c_installed AND C6_INSTALL_PKGCONF)
  # 与 c6.pc / c6lib.pc / libtcc.pc 同一套模板与安装机制（cmake/C6Metadata.cmake）
  c6_install_pc(c6c "c6c"
    "c6c - self-hosted compiler written in c6 (embeddable libc6c)"
    "-lc6c" "c6c=\${bindir}/c6c${C6_TCC_EXESUF}"
    NO_TCCDIR
    VARS_COMMENT "pkgconf --variable=c6c c6c")
endif()
