# c6c CMake 包配置（由 c6/CMakeLists.txt 生成，请勿手改）
#
#   find_package(c6c 0.1 REQUIRED COMPONENTS compiler libc6c)
#   target_link_libraries(app PRIVATE c6c::libc6c)
#   add_custom_command(... COMMAND c6c::compiler ...)
#
# c6c 是用 c6 语言自举重写的编译器，和 tcc 版 c6 是两个产品：这里只描述它自己
# 的二进制（bin/c6c）与嵌入库 libc6c（c6_* 嵌入 API + tcc_* 兼容层）。
# 要 libtcc 那套接口请找 find_package(c6)：有 tcc 时是真 libtcc，没有时由
# libc6c 顶替（c6Config 里那条兼容路径不受本文件影响）。
#
# @PACKAGE_*@ 在 find_package 时按实际安装前缀重定位，DESTDIR 暂存过的安装树
# 搬走后依然可用。


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was c6cConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

set(c6c_VERSION "0.6.4")
set(c6c_EXT_ENABLED ON)

set_and_check(c6c_BINDIR "${PACKAGE_PREFIX_DIR}/bin")
set_and_check(c6c_LIBDIR "${PACKAGE_PREFIX_DIR}/lib")
set_and_check(c6c_INCLUDE_DIR "${PACKAGE_PREFIX_DIR}/include")

# C6_ENABLE_TCC=OFF 时 c6c 同时装成 bin/c6 与 bin/c6c；有 tcc 时只有 bin/c6c。
set(c6c_EXECUTABLE "${c6c_BINDIR}/c6c")
set(c6c_LIBC6C_STATIC "${c6c_LIBDIR}/libc6c.a")
set(c6c_LIBC6C_SHARED "${c6c_LIBDIR}/libc6c.so")
set(c6c_LIBRARIES "${c6c_LIBC6C_STATIC}")
set(c6c_INCLUDE_DIRS "${c6c_INCLUDE_DIR}")

include("${CMAKE_CURRENT_LIST_DIR}/c6cTargets.cmake")

foreach(_c6c_comp IN LISTS c6c_FIND_COMPONENTS)
  if(_c6c_comp STREQUAL "compiler" AND EXISTS "${c6c_EXECUTABLE}")
    set(c6c_compiler_FOUND TRUE)
  elseif(_c6c_comp STREQUAL "libc6c" AND EXISTS "${c6c_LIBC6C_STATIC}")
    set(c6c_libc6c_FOUND TRUE)
  else()
    set(c6c_${_c6c_comp}_FOUND FALSE)
  endif()
endforeach()

check_required_components(c6c)
