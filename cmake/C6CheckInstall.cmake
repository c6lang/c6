# check-install：把 install() 结果装进 DESTDIR 暂存树，再校验安装清单、
# 用装出去的 c6 加 -B 编译运行、跑 find_package(c6) 消费方与版本负例、
# 核对 pkgconf 的 prefix 重定位。CI 与本地共用这一条验收链路。
if(NOT C6_ENABLE_TCC)
  add_custom_target(check-install
    COMMAND "${CMAKE_COMMAND}" -E echo
      "c6: check-install 校验的是 tcc 安装树；本次 C6_ENABLE_TCC=OFF"
    VERBATIM)
  return()
endif()
set(C6_INSTALL_PREFIX_REL "${CMAKE_INSTALL_PREFIX}")
string(REGEX REPLACE "^/+" "" C6_INSTALL_PREFIX_REL "${C6_INSTALL_PREFIX_REL}")
# c6c 的包配置落点（C6CInstall.cmake 只在"这次会装 c6c"时设 C6C_PACKAGE_DIR；
# 没设时留空，脚本里按空值跳过 find_package(c6c) 那一段）。
if(NOT DEFINED C6C_PACKAGE_DIR)
  set(C6C_PACKAGE_DIR "")
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6-check-install.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/c6-check-install.cmake" @ONLY)
add_custom_target(check-install
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/c6-check-install.cmake"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  COMMENT "c6: 校验 DESTDIR 安装树里的 CMake / pkgconf 库配置"
  VERBATIM USES_TERMINAL)
add_dependencies(check-install c6)
