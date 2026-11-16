# 生成并安装 CMake 包配置（find_package(c6)）与 pkgconf 元数据。
#
# 描述的对象按构建形态分两种：
#   * 有 tcc（C6_ENABLE_TCC=ON）：tcc 的 libtcc + 它的 libtcc1.a；
#   * 没有 tcc（OFF）：c6c 自己的嵌入库 libc6c（c6_* API + tcc_* 兼容层）。
# 两种都要把 find_package(c6) / pkgconf 元数据装对——"没有 tcc 就没有库"
# 这个前提在库形态落地以后就不成立了。
#
# c6c 自己的包名（find_package(c6c) / c6c.pc）见 C6CInstall.cmake；两边的
# 生成机制是同一套：cmake/C6Metadata.cmake 的 c6_install_cmake_package() 与
# c6_install_pc()。
#
# 注意：这里的 ${...} 是**配置期**展开，\${...} 才是留给 pkgconf 的变量引用。
if(C6_ENABLE_TCC)
  set(C6_PC_LIBS "-ltcc")
  set(C6_PC_VARS "c6=\${bindir}/c6${C6_TCC_EXESUF}\nlibtcc1=\${tccdir}/libtcc1.a")
else()
  set(C6_PC_LIBS "-lc6c")
  set(C6_PC_VARS "c6=\${bindir}/c6${C6_TCC_EXESUF}")
endif()

# ---------------------------------------------------------------------------
# 5. CMake 库配置：find_package(c6) -> c6::libtcc / c6::compiler
# ---------------------------------------------------------------------------

if(C6_INSTALL_CMAKE_PACKAGE)
  set(C6_BINDIR "${CMAKE_INSTALL_FULL_BINDIR}")
  set(C6_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
  set(C6_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
  set(C6_TCCDIR "${C6_INSTALL_TCCDIR}")
  # C6CheckInstall.cmake 的 check-install 要按这个落点核对安装树
  set(C6_PACKAGE_DIR "${C6_LIBDIR_REL}/cmake/c6")
  c6_install_cmake_package(c6 "${C6_PACKAGE_DIR}"
    c6Config.cmake.in c6Targets.cmake.in
    C6_BINDIR C6_LIBDIR C6_INCLUDEDIR C6_TCCDIR)
endif()

# ---------------------------------------------------------------------------
# 6. pkgconf 库配置：pkgconf --cflags --libs c6 / c6lib / libtcc
# ---------------------------------------------------------------------------

if(C6_INSTALL_PKGCONF)
  if(C6_ENABLE_TCC)
    set(_c6_pc_desc "c6 - TinyCC with language extensions (libtcc embedding API)")
  else()
    set(_c6_pc_desc "c6 - TinyCC-compatible C compiler (c6c embeddable library)")
  endif()
  c6_install_pc(c6 "c6" "${_c6_pc_desc}" "${C6_PC_LIBS}" "${C6_PC_VARS}")

  # c6c 的嵌入库：pkgconf --cflags --libs c6lib → -lc6c
  if(C6_INSTALL_C6LIB)
    c6_install_pc(c6lib "c6lib"
      "c6lib - c6c embeddable compiler (c6_* API + tcc_* compat)"
      "-lc6c" "c6=\${bindir}/c6${C6_TCC_EXESUF}")
  endif()

  if(C6_INSTALL_LIBTCC_PC AND C6_ENABLE_TCC)
    c6_install_pc(libtcc "libtcc"
      "libtcc - TinyCC/c6 embeddable C compiler library"
      "${C6_PC_LIBS}" "${C6_PC_VARS}")
  endif()
endif()
