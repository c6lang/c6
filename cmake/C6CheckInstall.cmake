# check-install：把 install() 结果装进 DESTDIR 暂存树，再校验安装清单、
# 用装出去的 c6 加 -B 编译运行、跑 find_package(c6) 消费方与版本负例、
# 核对 pkgconf 的 prefix 重定位。CI 与本地共用这一条验收链路。
set(C6_INSTALL_PREFIX_REL "${CMAKE_INSTALL_PREFIX}")
string(REGEX REPLACE "^/+" "" C6_INSTALL_PREFIX_REL "${C6_INSTALL_PREFIX_REL}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/c6-check-install.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/c6-check-install.cmake" @ONLY)
add_custom_target(check-install
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/c6-check-install.cmake"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  COMMENT "c6: 校验 DESTDIR 安装树里的 CMake / pkgconf 库配置"
  VERBATIM USES_TERMINAL)
add_dependencies(check-install c6)
