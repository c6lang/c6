# This file will be configured to contain variables for CPack. These variables
# should be set in the CMake list file of the project before CPack module is
# included. The list of available CPACK_xxx variables and their associated
# documentation may be obtained using
#  cpack --help-variable-list
#
# Some variables are common to all generators (e.g. CPACK_PACKAGE_NAME)
# and some are specific to a generator
# (e.g. CPACK_NSIS_EXTRA_INSTALL_COMMANDS). The generator specific variables
# usually begin with CPACK_<GENNAME>_xxxx.


set(CPACK_BUILD_SOURCE_DIRS "/media/winger/_dde_data/superc;/media/winger/_dde_data/superc/out")
set(CPACK_CMAKE_GENERATOR "Unix Makefiles")
set(CPACK_COMPONENT_UNSPECIFIED_HIDDEN "TRUE")
set(CPACK_COMPONENT_UNSPECIFIED_REQUIRED "TRUE")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_CONFLICTS "tcc")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17)")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/c6lang/c6")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "taotieren <admin@taotieren.com>")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_PROVIDES "tcc")
set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS "OFF")
set(CPACK_DEBIAN_PACKAGE_VERSION "0.6.0")
set(CPACK_DEFAULT_PACKAGE_DESCRIPTION_FILE "/usr/share/cmake-3.31/Templates/CPack.GenericDescription.txt")
set(CPACK_DEFAULT_PACKAGE_DESCRIPTION_SUMMARY "c6 built using CMake")
set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE "ON")
set(CPACK_GENERATOR "TGZ")
set(CPACK_INNOSETUP_ARCHITECTURE "x64")
set(CPACK_INSTALL_CMAKE_PROJECTS "/media/winger/_dde_data/superc/out;c6;ALL;/")
set(CPACK_INSTALL_PREFIX "/usr/local")
set(CPACK_MODULE_PATH "")
set(CPACK_NSIS_DISPLAY_NAME "c6 0.6.0")
set(CPACK_NSIS_INSTALLER_ICON_CODE "")
set(CPACK_NSIS_INSTALLER_MUI_ICON_CODE "")
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")
set(CPACK_NSIS_PACKAGE_NAME "c6 0.6.0")
set(CPACK_NSIS_UNINSTALL_NAME "Uninstall")
set(CPACK_OBJCOPY_EXECUTABLE "/usr/bin/objcopy")
set(CPACK_OBJDUMP_EXECUTABLE "/usr/bin/objdump")
set(CPACK_OUTPUT_CONFIG_FILE "/media/winger/_dde_data/superc/out/CPackConfig.cmake")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_PACKAGE_CONTACT "taotieren <admin@taotieren.com>")
set(CPACK_PACKAGE_DEFAULT_LOCATION "/")
set(CPACK_PACKAGE_DESCRIPTION "c6 is a small, fast C compiler derived from TinyCC, extended with
modules, generics, slices, closures, async and a C99 output backend.
This package ships the c6 compiler, the embeddable libtcc library,
the runtime library (libtcc1.a), headers, CMake package config,
pkgconf metadata and the c6 documentation under share/doc/c6.
")
set(CPACK_PACKAGE_DESCRIPTION_FILE "/usr/share/cmake-3.31/Templates/CPack.GenericDescription.txt")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "c6 - TinyCC with language extensions")
set(CPACK_PACKAGE_FILE_NAME "c6-0.6.0-1-x86_64")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/c6lang/c6")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "c6 0.6.0")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "c6 0.6.0")
set(CPACK_PACKAGE_NAME "c6")
set(CPACK_PACKAGE_RELOCATABLE "true")
set(CPACK_PACKAGE_VENDOR "c6lang")
set(CPACK_PACKAGE_VERSION "0.6.0")
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "6")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
set(CPACK_READELF_EXECUTABLE "/usr/bin/readelf")
set(CPACK_RESOURCE_FILE_LICENSE "/media/winger/_dde_data/superc/tcc/COPYING")
set(CPACK_RESOURCE_FILE_README "/media/winger/_dde_data/superc/README.md")
set(CPACK_RESOURCE_FILE_WELCOME "/usr/share/cmake-3.31/Templates/CPack.GenericWelcome.txt")
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
set(CPACK_RPM_PACKAGE_AUTOPROV "OFF")
set(CPACK_RPM_PACKAGE_AUTOREQ "OFF")
set(CPACK_RPM_PACKAGE_CONFLICTS "tcc")
set(CPACK_RPM_PACKAGE_DESCRIPTION "c6 is a small, fast C compiler derived from TinyCC, extended with
modules, generics, slices, closures, async and a C99 output backend.
This package ships the c6 compiler, the embeddable libtcc library,
the runtime library (libtcc1.a), headers, CMake package config,
pkgconf metadata and the c6 documentation under share/doc/c6.
")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_LICENSE "LGPL-2.1-or-later")
set(CPACK_RPM_PACKAGE_PROVIDES "tcc")
set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_RELOCATABLE "OFF")
set(CPACK_RPM_PACKAGE_REQUIRES "glibc")
set(CPACK_RPM_PACKAGE_SUMMARY "c6 - TinyCC with language extensions")
set(CPACK_RPM_PACKAGE_URL "https://github.com/c6lang/c6")
set(CPACK_SET_DESTDIR "OFF")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES "/\\.git/;/build/;/build-.*/;/packages/;/stage/;/tcc/config\\.mak$;/tcc/config\\.h$;/tcc/\\.c6-;/tcc/c6$;/tcc/libtcc\\.(a|so|dylib);/tcc/libtcc1.*\\.a$;/tcc/config\\.texi$;/tcc/tcc\\.1$;/tcc/tcc-doc\\.(info|html)$;/\\.o$;/\\.a$;/\\.so;/\\.dylib$;/\\.exe$;/\\.core$;/bench/async/.*/target/")
set(CPACK_SOURCE_OUTPUT_CONFIG_FILE "/media/winger/_dde_data/superc/out/CPackSourceConfig.cmake")
set(CPACK_SYSTEM_NAME "Linux")
set(CPACK_THREADS "1")
set(CPACK_TOPLEVEL_TAG "Linux")
set(CPACK_WIX_SIZEOF_VOID_P "8")

if(NOT CPACK_PROPERTIES_FILE)
  set(CPACK_PROPERTIES_FILE "/media/winger/_dde_data/superc/out/CPackProperties.cmake")
endif()

if(EXISTS ${CPACK_PROPERTIES_FILE})
  include(${CPACK_PROPERTIES_FILE})
endif()
