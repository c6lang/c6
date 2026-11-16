# 安装：复刻 tcc/Makefile 的 install-unx 清单，支持 DESTDIR 与 --prefix 落点。

# ---------------------------------------------------------------------------
# c6c（c6 写的自举编译器）与它自己的数据文件。
#
# c6c 不认 tcc 的 <prefix>/lib/tcc，它按 argv[0] 推自己的目录找东西：
#   * 自带头文件：<bin>/../lib/c6/include（见 src/main.c6 的 setup_incdirs）；
#   * 运行时源码：<bin>/../lib/rt.c6 或 <bin>/../lib/c6/rt.c6
#     （见 src/parse.c6 的 find_runtime）。
# 所以「二进制 + 自带头文件 + rt.c6」必须成套出现：只装二进制的话它连 hello
# 都编不出来，只会报「找不到自带运行时 lib/rt.c6」。二进制搬家也能自己找到，
# 不依赖构建期的绝对路径。
#
# 三种配置共用这份清单：
#   * C6_ENABLE_TCC=OFF：没有 tcc，c6c 就是 c6 —— 装成 bin/c6，再加一个同内容
#     的 bin/c6c，让两种配置的落点一致；
#   * 默认（tcc 在）+ C6_INSTALL_C6C=ON：c6c 当第二个编译器装成 bin/c6c，
#     和 tcc 版 c6 并存（包里既能用 tcc 版 c6，也能用 c6c 自举）；
#   * C6_INSTALL_C6C=OFF：不装 c6c，只装 tcc 版 c6。
# 嵌入库 libc6c.a/.so 与它的头文件在 C6C.cmake 里装。
# ---------------------------------------------------------------------------
if(NOT C6_ENABLE_TCC)
  install(PROGRAMS "${C6C_DIR}/build/c6c"
          DESTINATION "${C6_BINDIR_REL}"
          RENAME "c6${C6_TCC_EXESUF}")
endif()
if(C6_INSTALL_C6C OR NOT C6_ENABLE_TCC)
  install(PROGRAMS "${C6C_DIR}/build/c6c"
          DESTINATION "${C6_BINDIR_REL}"
          RENAME "c6c${C6_TCC_EXESUF}")

  # 自带头文件（stddef.h / stdarg.h / stdint.h …）装到 <prefix>/lib/c6/include，
  # 不装进 <prefix>/include：那一层是给**宿主 C 程序**用的（c6lib.h / libtcc.h），
  # 把 c6 自己的 stdio.h 塞进去会把系统头文件盖掉，pkgconf 的 Cflags 就没法用了。
  file(GLOB _c6c_hdrs "${C6C_DIR}/include/*.h")
  install(FILES ${_c6c_hdrs} DESTINATION "${C6_LIBDIR_REL}/c6/include")
  install(DIRECTORY "${C6C_DIR}/include/sys"
          DESTINATION "${C6_LIBDIR_REL}/c6/include")

  # 自带运行时源码：编译时被拼进 TU，装在 <prefix>/lib/c6/rt.c6 才找得到。
  install(FILES "${C6C_DIR}/lib/rt.c6" DESTINATION "${C6_LIBDIR_REL}/c6")
endif()

# C6_ENABLE_TCC=OFF：没有 tcc 的 install-unx 清单可抄，装到这里为止；剩下的
# 是 tcc 的库、头文件与文档。
if(NOT C6_ENABLE_TCC)
  return()
endif()

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

# ---------------------------------------------------------------------------
# c6 自己的文档：语言扩展、std、测试与后端只在这里有说明，和上游 tcc 的
# tcc-doc.html 分开装。保留 docs/ 子目录结构，文档内 reference/*.md 之类的
# 相对链接才能继续用；根 README / CHANGELOG 放在它的上一层，正好对应
# docs/README.md 的 ../README.md 与 docs/history/ 的 ../../CHANGELOG.md。
# ---------------------------------------------------------------------------
if(C6_INSTALL_DOCS)
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/"
          DESTINATION "${C6_C6_DOCDIR_REL}/docs"
          FILES_MATCHING PATTERN "*.md")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
                "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md"
          DESTINATION "${C6_C6_DOCDIR_REL}")
  # Debian 惯例 /usr/share/doc/<pkg>/copyright；CPack DEB 不会自动生成。
  install(FILES "${C6_TCC_DIR}/COPYING"
          DESTINATION "${C6_C6_DOCDIR_REL}" RENAME "copyright" OPTIONAL)
endif()

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
