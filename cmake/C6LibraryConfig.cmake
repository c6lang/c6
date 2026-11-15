# 生成并安装 CMake 包配置（find_package(c6)）与 pkgconf 元数据。

# 描述的对象按构建形态分两种：
#   * 有 tcc（C6_ENABLE_TCC=ON）：tcc 的 libtcc + 它的 libtcc1.a；
#   * 没有 tcc（OFF）：c6c 自己的嵌入库 libc6c（c6_* API + tcc_* 兼容层）。
# 两种都要把 find_package(c6) / pkgconf 元数据装对——"没有 tcc 就没有库"
# 这个前提在库形态落地以后就不成立了。
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
  if(C6_ENABLE_TCC)
    set(C6_PC_DESCRIPTION "c6 - TinyCC with language extensions (libtcc embedding API)")
  else()
    set(C6_PC_DESCRIPTION "c6 - TinyCC-compatible C compiler (c6c embeddable library)")
  endif()
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6.pc.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/c6.pc" @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/c6.pc"
          DESTINATION "${C6_PKGCONFIG_DIR_REL}")

  # c6c 的嵌入库：pkgconf --cflags --libs c6lib → -lc6c
  if(C6_INSTALL_C6LIB)
    set(_c6_pc_libs_saved "${C6_PC_LIBS}")
    set(_c6_pc_vars_saved "${C6_PC_VARS}")
    set(C6_PC_NAME "c6lib")
    set(C6_PC_DESCRIPTION "c6lib - c6c embeddable compiler (c6_* API + tcc_* compat)")
    set(C6_PC_LIBS "-lc6c")
    set(C6_PC_VARS "c6=\${bindir}/c6${C6_TCC_EXESUF}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6.pc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/c6lib.pc" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/c6lib.pc"
            DESTINATION "${C6_PKGCONFIG_DIR_REL}")
    set(C6_PC_LIBS "${_c6_pc_libs_saved}")
    set(C6_PC_VARS "${_c6_pc_vars_saved}")
  endif()

  if(C6_INSTALL_LIBTCC_PC AND C6_ENABLE_TCC)
    set(C6_PC_NAME "libtcc")
    set(C6_PC_DESCRIPTION "libtcc - TinyCC/c6 embeddable C compiler library")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6.pc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/libtcc.pc" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libtcc.pc"
            DESTINATION "${C6_PKGCONFIG_DIR_REL}")
  endif()
endif()
