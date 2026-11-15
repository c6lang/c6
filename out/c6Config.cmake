# c6 CMake 包配置（由 c6/CMakeLists.txt 生成，请勿手改）
#
#   find_package(c6 0.1 REQUIRED COMPONENTS libtcc)
#   target_link_libraries(app PRIVATE c6::libtcc)
#   add_custom_command(... COMMAND c6::compiler ...)
#
# libtcc 由 tcc/Makefile 编译，不是 CMake 原生 target，所以这里以 IMPORTED
# target 描述已经安装的文件。@PACKAGE_*@ 在 find_package 时按实际安装前缀
# 重定位，因此 DESTDIR 暂存过的安装树搬走后依然可用。


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was c6Config.cmake.in                            ########

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

set(C6_VERSION "0.6.0")
set(C6_EXT_ENABLED ON)
set(C6_STATIC_LIBTCC TRUE)
set(C6_LIBTCC_FILENAME "libtcc.a")

set_and_check(C6_BINDIR "${PACKAGE_PREFIX_DIR}/bin")
set_and_check(C6_LIBDIR "${PACKAGE_PREFIX_DIR}/lib")
set_and_check(C6_INCLUDE_DIR "${PACKAGE_PREFIX_DIR}/include")
set_and_check(C6_TCCDIR "${PACKAGE_PREFIX_DIR}/lib/tcc")

set(C6_EXECUTABLE "${C6_BINDIR}/c6")
set(C6_LIBTCC_LIBRARY "${C6_LIBDIR}/libtcc.a")
set(C6_LIBRARIES "${C6_LIBTCC_LIBRARY}")
set(C6_INCLUDE_DIRS "${C6_INCLUDE_DIR}")

include("${CMAKE_CURRENT_LIST_DIR}/c6Targets.cmake")

foreach(_c6_comp IN LISTS c6_FIND_COMPONENTS)
  if(_c6_comp STREQUAL "libtcc" AND EXISTS "${C6_LIBTCC_LIBRARY}")
    set(c6_libtcc_FOUND TRUE)
  elseif(_c6_comp STREQUAL "compiler" AND EXISTS "${C6_EXECUTABLE}")
    set(c6_compiler_FOUND TRUE)
  else()
    set(c6_${_c6_comp}_FOUND FALSE)
  endif()
endforeach()

check_required_components(c6)
