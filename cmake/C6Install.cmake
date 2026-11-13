# 安装：复刻 tcc/Makefile 的 install-unx 清单，支持 DESTDIR 与 --prefix 落点。

# ---------------------------------------------------------------------------
# 4. 安装：与 tcc/Makefile 的 install-unx 同一套文件，但由 CMake 自己装，
#    文件落点 / DESTDIR 按 CMake 语义生效，且与生成的库配置共用一套路径。
#    注意二进制里的 CONFIG_TCCDIR、rpath 由 tcc/CMakeLists.txt 在配置阶段写入
#    config.h / config.mak，不能用 cmake --install --prefix 改；换前缀要重跑
#    cmake -S . -B build 再 cmake --build build。
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
