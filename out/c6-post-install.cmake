# 由 c6/CMakeLists.txt 生成：安装 bindir/tcc -> c6 的兼容软链
# AUR 包 c6-git provides/conflicts tcc，这条软链让 tcc 命令继续可用。
set(_c6_link "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/tcc")
get_filename_component(_c6_link_dir "${_c6_link}" DIRECTORY)
file(MAKE_DIRECTORY "${_c6_link_dir}")
execute_process(COMMAND "/usr/bin/cmake" -E rm -f "${_c6_link}")
execute_process(
  COMMAND "/usr/bin/cmake" -E create_symlink "c6" "${_c6_link}"
  RESULT_VARIABLE _c6_link_ret)
if(NOT _c6_link_ret EQUAL 0)
  message(FATAL_ERROR "c6: 创建 ${_c6_link} 软链失败")
endif()
