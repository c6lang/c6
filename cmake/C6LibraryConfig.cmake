# 生成并安装 CMake 包配置（find_package(c6)）与 pkgconf 元数据。

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
