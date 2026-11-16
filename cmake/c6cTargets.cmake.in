# c6c IMPORTED targets（由 c6/CMakeLists.txt 生成，请勿手改）
# 由 c6cConfig.cmake include；c6c_EXECUTABLE / c6c_LIBC6C_* 在那里定义。

if(NOT DEFINED c6c_EXECUTABLE)
  message(FATAL_ERROR
    "c6cTargets.cmake 必须由 find_package(c6c) 通过 c6cConfig.cmake 引入")
endif()

if(NOT TARGET c6c::compiler AND EXISTS "${c6c_EXECUTABLE}")
  add_executable(c6c::compiler IMPORTED)
  set_target_properties(c6c::compiler PROPERTIES
    IMPORTED_LOCATION "${c6c_EXECUTABLE}")
endif()

# 静态库是默认链接形态：libc6c.so 没有 SONAME，直接链 .so 会把绝对路径写进
# 消费者二进制；要动态链接就显式用 c6c::libc6c_shared。
if(NOT TARGET c6c::libc6c AND EXISTS "${c6c_LIBC6C_STATIC}")
  add_library(c6c::libc6c STATIC IMPORTED)
  set_target_properties(c6c::libc6c PROPERTIES
    IMPORTED_LOCATION "${c6c_LIBC6C_STATIC}"
    INTERFACE_INCLUDE_DIRECTORIES "${c6c_INCLUDE_DIR}")
endif()

if(NOT TARGET c6c::libc6c_shared AND EXISTS "${c6c_LIBC6C_SHARED}")
  add_library(c6c::libc6c_shared SHARED IMPORTED)
  set_target_properties(c6c::libc6c_shared PROPERTIES
    IMPORTED_LOCATION "${c6c_LIBC6C_SHARED}"
    INTERFACE_INCLUDE_DIRECTORIES "${c6c_INCLUDE_DIR}")
endif()

set(c6c_LIBRARIES c6c::libc6c)
set(c6c_INCLUDE_DIRS "${c6c_INCLUDE_DIR}")
