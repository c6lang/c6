# CPack：c6 的 TGZ / DEB / RPM 打包配置。
# 由 CMakeLists.txt 在 install() 规则之后 include；CPack 会复用同一套
# install() 清单，所以包内容与 `cmake --install` / AUR PKGBUILD 完全一致。
#
#   cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
#   cmake --build build --parallel
#   cpack --config build/CPackConfig.cmake -G DEB -B packages
#   cpack --config build/CPackConfig.cmake -G RPM -B packages

# ---------------------------------------------------------------------------
# 版本：tcc/VERSION 是 0.9.28rc，rc 后缀不能直接当 deb/rpm 的正式版本。
# 用数值段 0.9.28 做 Version，预发布标记放进 deb 的 ~rc 与 rpm 的 Release，
# 这样 deb 的排序（0.9.28~rc < 0.9.28）才是正确的。
# ---------------------------------------------------------------------------

set(C6_PACKAGE_RELEASE "1" CACHE STRING
    "DEB/RPM 的 Release 字段；CI 可注入构建号，例如 42.1a2b3c4")
set(C6_PACKAGE_PRERELEASE "" CACHE STRING
    "预发布标记（如 rc）；留空则从 C6_VERSION_RAW 自动推断")

if(NOT C6_PACKAGE_PRERELEASE)
  string(REGEX MATCH "^[0-9]+(\\.[0-9]+)+(.+)$" _c6_ver_match "${C6_VERSION_RAW}")
  if(_c6_ver_match)
    set(C6_PACKAGE_PRERELEASE "${CMAKE_MATCH_2}")
  endif()
endif()

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _c6_num_match
       "${C6_VERSION_NUMERIC}")
set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_2}")
set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_3}")

set(CPACK_PACKAGE_NAME "c6")
set(CPACK_PACKAGE_VERSION "${C6_VERSION_NUMERIC}")
set(CPACK_PACKAGE_VENDOR "c6lang")
set(CPACK_PACKAGE_CONTACT "taotieren <admin@taotieren.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/c6lang/c6")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "c6 - TinyCC with language extensions")
string(CONCAT CPACK_PACKAGE_DESCRIPTION
  "c6 is a small, fast C compiler derived from TinyCC, extended with\n"
  "modules, generics, slices, closures, async and a C99 output backend.\n"
  "This package ships the c6 compiler, the embeddable libtcc library,\n"
  "the runtime library (libtcc1.a), headers, CMake package config and\n"
  "pkgconf metadata.\n")
set(CPACK_RESOURCE_FILE_LICENSE "${C6_TCC_DIR}/COPYING")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# install() 用的是相对 prefix 的路径，包内一律以 /usr 为根。
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_CHECKSUM "SHA256")

# 默认只开 TGZ，避免本机没有 rpmbuild/dpkg-shlibdeps 时 `cpack` 直接失败；
# CI 用 `cpack -G DEB` / `-G RPM` 显式覆盖。
set(CPACK_GENERATOR "TGZ" CACHE STRING "cpack 默认生成器，分号分隔")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/" "/build/" "/build-.*/" "/packages/" "/stage/"
    "/tcc/config\\\\.mak$" "/tcc/config\\\\.h$" "/tcc/\\\\.c6-"
    "/\\\\.o$" "/\\\\.a$" "/\\\\.so" "/\\\\.dylib$" "/\\\\.exe$")

# ---------------------------------------------------------------------------
# DEB
# ---------------------------------------------------------------------------

if(C6_PACKAGE_PRERELEASE)
  set(_c6_deb_version "${C6_VERSION_NUMERIC}~${C6_PACKAGE_PRERELEASE}")
else()
  set(_c6_deb_version "${C6_VERSION_NUMERIC}")
endif()
set(CPACK_DEBIAN_PACKAGE_VERSION "${_c6_deb_version}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${C6_PACKAGE_RELEASE}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "taotieren <admin@taotieren.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17)")
set(CPACK_DEBIAN_PACKAGE_PROVIDES "tcc")
set(CPACK_DEBIAN_PACKAGE_CONFLICTS "tcc")
# 不探测本机的 dpkg：没有 dpkg 时 CPack 会悄悄默认成 i386。直接从
# CMake 的目标架构映射，保证在任何打包机上架构都是对的。
set(_c6_deb_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(_c6_deb_arch STREQUAL "x86_64")
  set(_c6_deb_arch "amd64")
elseif(_c6_deb_arch MATCHES "^(aarch64|arm64)$")
  set(_c6_deb_arch "arm64")
elseif(_c6_deb_arch MATCHES "^armv")
  set(_c6_deb_arch "armhf")
elseif(_c6_deb_arch MATCHES "^(i[3-6]86|x86)$")
  set(_c6_deb_arch "i386")
endif()
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${_c6_deb_arch}")
# 编译器是按 -g -O0 编的且静态/共享两种模式的可执行文件不同，
# 不让 dpkg-shlibdeps 在打包机上重新推导依赖。
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)

# ---------------------------------------------------------------------------
# RPM
# ---------------------------------------------------------------------------

if(C6_PACKAGE_PRERELEASE)
  set(_c6_rpm_release "0.${C6_PACKAGE_RELEASE}.${C6_PACKAGE_PRERELEASE}")
else()
  set(_c6_rpm_release "${C6_PACKAGE_RELEASE}")
endif()
set(CPACK_RPM_PACKAGE_RELEASE "${_c6_rpm_release}")
set(CPACK_RPM_PACKAGE_LICENSE "LGPL-2.1-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_SUMMARY "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
set(CPACK_RPM_PACKAGE_REQUIRES "glibc")
set(CPACK_RPM_PACKAGE_PROVIDES "tcc")
set(CPACK_RPM_PACKAGE_CONFLICTS "tcc")
set(CPACK_RPM_PACKAGE_AUTOREQ OFF)
set(CPACK_RPM_PACKAGE_AUTOPROV OFF)
set(CPACK_RPM_PACKAGE_RELOCATABLE OFF)

include(CPack)
