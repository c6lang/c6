# 安装元数据的共用机制：pkgconf `.pc`、`find_package(...)` 包配置，以及
# "执行构建树里的 c6 时要补的环境变量"。
#
# c6（C6LibraryConfig.cmake / C6Testing.cmake / C6Wasm.cmake）与 c6c
# （C6C.cmake / C6CInstall.cmake）走**同一套**机制，差别只在传进来的名字、
# 模板与变量。以前这套东西在三个模块里各写一遍（`.pc` 的 prefix 反推、配置
# 文件的四件套、LD_LIBRARY_PATH），改一处得记得改四处。
#
# 由 CMakeLists.txt 在 C6Options.cmake 之后 include。

# ---------------------------------------------------------------------------
# 环境：执行构建树里的 c6 要补 LD_LIBRARY_PATH
# ---------------------------------------------------------------------------
# shared 形态（-DC6_TCC_SHARED_LIBTCC=ON）下构建树里的 c6 自己链到 libtcc.so，
# 但没有 rpath，直接执行会报找不到库。静态形态不用补。
#
#   c6_ld_env(<list 变量>)
#
# 结果**追加**到给定变量（调用方可能已经放了自己的 ENV 项），变量不存在时
# 当作空表。
function(c6_ld_env out)
  set(_acc ${${out}})
  if(C6_ENABLE_TCC AND NOT C6_TCC_STATIC)
    set(_p "${C6_TCC_DIR}:$ENV{LD_LIBRARY_PATH}")
    string(REGEX REPLACE ":+$" "" _p "${_p}")
    list(APPEND _acc "LD_LIBRARY_PATH=${_p}")
  endif()
  set(${out} "${_acc}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# pkgconf
# ---------------------------------------------------------------------------
# .pc 文件里的 prefix：用 ${pcfiledir} 反推安装前缀，装到别的前缀 / DESTDIR
# 暂存树里也能直接用（pkgconf 会按 pc 文件自己的位置展开 pcfiledir）。
function(c6_pcfile_prefix out pkgconfig_dir)
  file(RELATIVE_PATH _rel "${pkgconfig_dir}" "${CMAKE_INSTALL_PREFIX}")
  string(REGEX REPLACE "/+$" "" _rel "${_rel}")
  if(_rel STREQUAL "" OR _rel STREQUAL ".")
    set(${out} "\${pcfiledir}" PARENT_SCOPE)
  else()
    set(${out} "\${pcfiledir}/${_rel}" PARENT_SCOPE)
  endif()
endfunction()

# c6_install_pc(<pc 名> <包名> <描述> <Libs 行> <变量行>)
#
# 所有包共用 cmake/c6.pc.in（变量全是占位符）；"变量行"写 pkgconf --variable=
# 能查到的东西，例如 "c6=\${bindir}/c6"。调用方负责 C6_INSTALL_PKGCONF 开关。
function(c6_install_pc pcname name description libs vars)
  # 可选：NO_TCCDIR（c6c 没有 tcc 运行时库目录，不写 tccdir= 那一行）/
  #       VARS_COMMENT=<"变量"注释里的查询示例>
  cmake_parse_arguments(P "NO_TCCDIR" "VARS_COMMENT" "" ${ARGN})
  if(P_NO_TCCDIR)
    set(P_TCCDIR "")
  else()
    # 末尾带换行：模板里这一行整行由它决定（c6c 传空则不占行）
    set(P_TCCDIR "tccdir=\${prefix}/${C6_TCCDIR_REL}\n")
  endif()
  if(NOT P_VARS_COMMENT)
    set(P_VARS_COMMENT "pkgconf --variable=c6 c6 / pkgconf --variable=tccdir c6")
  endif()
  set(C6_PC_TCCDIR_VAR "${P_TCCDIR}")
  set(C6_PC_VARS_COMMENT "# 变量：${P_VARS_COMMENT}")
  c6_pcfile_prefix(_prefix "${C6_PKGCONFIG_DIR}")
  set(C6_PC_PREFIX "${_prefix}")
  set(C6_PC_NAME "${name}")
  set(C6_PC_DESCRIPTION "${description}")
  set(C6_PC_LIBS "${libs}")
  set(C6_PC_VARS "${vars}")
  configure_file("${C6_MODULE_DIR}/c6.pc.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/${pcname}.pc" @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${pcname}.pc"
          DESTINATION "${C6_PKGCONFIG_DIR_REL}")
endfunction()

# ---------------------------------------------------------------------------
# find_package(...) 包配置
# ---------------------------------------------------------------------------
# c6_install_cmake_package(<包名> <安装目录> <config 模板> <targets 模板>
#                          [PATH_VARS 变量...])
#
# 生成 <包名>Config.cmake / <包名>ConfigVersion.cmake / <包名>Targets.cmake
# 三件套并安装到 <安装目录>。<PATH_VARS> 里给的变量在模板中写作
# @PACKAGE_<变量名>@，会被重定位成"相对安装前缀"的形式。调用方负责
# C6_INSTALL_CMAKE_PACKAGE 开关。
function(c6_install_cmake_package pkg dest_rel config_in targets_in)
  configure_package_config_file(
    "${C6_MODULE_DIR}/${config_in}"
    "${CMAKE_CURRENT_BINARY_DIR}/${pkg}Config.cmake"
    INSTALL_DESTINATION "${dest_rel}"
    PATH_VARS ${ARGN}
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${pkg}ConfigVersion.cmake"
    VERSION "${C6_VERSION_NUMERIC}"
    COMPATIBILITY SameMajorVersion
  )
  configure_file("${C6_MODULE_DIR}/${targets_in}"
                 "${CMAKE_CURRENT_BINARY_DIR}/${pkg}Targets.cmake" @ONLY)
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${pkg}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${pkg}ConfigVersion.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${pkg}Targets.cmake"
    DESTINATION "${dest_rel}")
endfunction()
