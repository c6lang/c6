# tcc 的 CMake 配置与构建包装 —— 替代 ./configure。
#
# 设计边界（有意为之，不是"迁移未完成"）：
#   * 配置由 CMake 生成：config.mak / config.h / config.texi，规则对齐
#     上游 tcc/configure；configure 脚本本身不参与构建。
#   * 编译仍交给上游 tcc/Makefile。它承载 c2str / objcopy 双后端嵌入 /
#     libtcc1.a 用刚生成的 c6 自举 / 交叉子 make 等链，改写成纯 CMake 会
#     变成对上游 Makefile 的逐条复刻，维护成本更高。
#   * tcc/Makefile 要求 config.mak 位于 $(TOP)、产物位于源码目录，所以
#     生成的配置文件会被同步到 tcc/。但同步是「构建阶段由 CMake 依赖管理
#     驱动的 add_custom_command」，配置阶段对源码树零写入、零副作用。
#   * 由此带来的限制：这不是纯 out-of-source 构建，同一份 tcc 源码树同时
#     只应有一个 CMake 构建目录。配置阶段只读地检查源码树里残留的
#     owner 标记并给出警告；构建阶段每次比对源码树与生成目录的配置，
#     发现被别的构建目录覆盖就重新同步并清理旧目标文件，不会静默混用。
#
# 用法：父项目 add_subdirectory(tcc)，或独立 cmake -S tcc -B build。
# 所有 TCC_* 输入变量都可在 add_subdirectory 之前设置，留空则用与
# configure 相同的默认值。构建期产物通过 IMPORTED target 暴露：
#   tcc::c6       可执行文件
#   tcc::libtcc   静态/共享库（含 include 目录与链接依赖）

cmake_minimum_required(VERSION 3.20)

set(TCC_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
file(READ "${TCC_SOURCE_DIR}/VERSION" TCC_VERSION)
string(STRIP "${TCC_VERSION}" TCC_VERSION)
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)+" TCC_VERSION_NUMERIC "${TCC_VERSION}")

# 可选的 c6 发布版本：父项目（仓库根 CMakeLists.txt）按根目录 VERSION 传入，
# 写进 config.h 的 C6_VERSION，由 `c6 -v` 打印。这里刻意不读 tcc/VERSION ——
# 那是内嵌上游 TinyCC 的兼容版本，跟 c6 自己的发布号是两件事。
# 独立构建 tcc/ 时没有这个变量，config.h 里也就没有 C6_VERSION 宏。
if(NOT DEFINED C6_RELEASE_VERSION)
  set(C6_RELEASE_VERSION "")
endif()
if(C6_RELEASE_VERSION)
  set(TCC_C6_VERSION_DEFINE "#define C6_VERSION \"${C6_RELEASE_VERSION}\"")
else()
  set(TCC_C6_VERSION_DEFINE "")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(tcc VERSION "${TCC_VERSION_NUMERIC}" LANGUAGES C
          DESCRIPTION "TinyCC/c6 compiler" HOMEPAGE_URL "https://github.com/c6lang/c6")
endif()

include(GNUInstallDirs)
include(ProcessorCount)

# tcc/Makefile 的 $(TOP) 与 -B$(TOP) 都指向源码目录；产物路径与源码路径
# 在这里是同一个目录，用单独的变量名表达语义，避免和 CMake 的
# CMAKE_CURRENT_BINARY_DIR 混淆。
set(TCC_OUTPUT_DIR "${TCC_SOURCE_DIR}")
set(TCC_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# ---------------------------------------------------------------------------
# 1. 输入：安装目录、编译器、编译开关
# ---------------------------------------------------------------------------

if(NOT TCC_INSTALL_PREFIX)
  set(TCC_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
endif()
if(NOT TCC_INSTALL_BINDIR)
  set(TCC_INSTALL_BINDIR "${CMAKE_INSTALL_FULL_BINDIR}")
endif()
if(NOT TCC_INSTALL_LIBDIR)
  set(TCC_INSTALL_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
endif()
if(NOT TCC_INSTALL_TCCDIR)
  set(TCC_INSTALL_TCCDIR "${CMAKE_INSTALL_FULL_LIBDIR}/tcc")
endif()
if(NOT TCC_INSTALL_INCLUDEDIR)
  set(TCC_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
endif()
if(NOT TCC_INSTALL_MANDIR)
  set(TCC_INSTALL_MANDIR "${CMAKE_INSTALL_FULL_MANDIR}")
endif()
if(NOT TCC_INSTALL_INFODIR)
  set(TCC_INSTALL_INFODIR "${CMAKE_INSTALL_FULL_INFODIR}")
endif()
if(NOT TCC_INSTALL_DOCDIR)
  set(TCC_INSTALL_DOCDIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/doc/tcc")
endif()

if(NOT TCC_CC)
  set(TCC_CC "${CMAKE_C_COMPILER}")
endif()
if(NOT TCC_AR)
  set(TCC_AR "${CMAKE_AR}")
endif()
if(NOT TCC_MAKE_EXECUTABLE)
  find_program(TCC_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
endif()
if(NOT TCC_JOBS)
  # 跟随 cmake --build --parallel；没给就退回 CPU 核数（仅 Ninja 等
  # 没有 jobserver 的生成器会用到，见下面的 _tcc_make_invocation）。
  if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL} AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
    set(TCC_JOBS "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
  else()
    ProcessorCount(_tcc_ncpu)
    if(_tcc_ncpu EQUAL 0)
      set(_tcc_ncpu 4)
    endif()
    set(TCC_JOBS "${_tcc_ncpu}")
  endif()
endif()

# 写进 config.mak 的默认 CFLAGS（configure 的默认 + 它探测出的两个 -W 选项）。
# 主构建可以用 TCC_BUILD_CFLAGS 作为 make 命令行变量覆盖它。
if(NOT TCC_CONFIG_CFLAGS)
  set(TCC_CONFIG_CFLAGS "-Wall -O2 -Wdeclaration-after-statement -Wno-unused-result")
endif()
if(NOT DEFINED TCC_LDFLAGS)
  set(TCC_LDFLAGS "")
endif()

# TCC_EXTRA_DEFS        -> EXTRA-DEFS=...（c6 的 -DC6_EXT 从这里进来）
# TCC_BUILD_CFLAGS      -> CFLAGS=...（命令行覆盖）
# TCC_MAKE_ARGS         -> 额外 VAR=值，CMake 列表
# TCC_EXTRA_CONFIG_VARS -> 额外 CONFIG_x=y 行，CMake 列表（原 --config-*）
if(NOT DEFINED TCC_SHARED_LIBTCC)
  set(TCC_SHARED_LIBTCC OFF)
endif()

# ---------------------------------------------------------------------------
# 2. 平台探测：CMake 原生信息 + 一次编译器查询
# ---------------------------------------------------------------------------

# 编译器身份必须以实际写进 config.mak 的 TCC_CC 为准，而不是
# CMAKE_C_COMPILER_ID：C6_TCC_CC 可以单独指定编译器，混用会让 CC_NAME /
# GCC_MAJOR 与真实编译器对不上。
if(TCC_CC STREQUAL CMAKE_C_COMPILER)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(TCC_CC_NAME "clang")
  else()
    set(TCC_CC_NAME "gcc")
  endif()
  set(_tcc_cc_banner "${CMAKE_C_COMPILER_VERSION}")
else()
  execute_process(COMMAND "${TCC_CC}" --version
    OUTPUT_VARIABLE _tcc_cc_banner OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(_tcc_cc_banner MATCHES "[Cc]lang")
    set(TCC_CC_NAME "clang")
  else()
    set(TCC_CC_NAME "gcc")
  endif()
endif()
string(REGEX MATCH "([0-9]+)\\.([0-9]+)" _unused "${_tcc_cc_banner}")
set(TCC_GCC_MAJOR "${CMAKE_MATCH_1}")
set(TCC_GCC_MINOR "${CMAKE_MATCH_2}")
if(NOT TCC_GCC_MAJOR)
  set(TCC_GCC_MAJOR 0)
endif()
if(NOT TCC_GCC_MINOR)
  set(TCC_GCC_MINOR 0)
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _tcc_cpu)
if(_tcc_cpu MATCHES "^(x86_64|amd64)$")
  set(TCC_ARCH "x86_64")
elseif(_tcc_cpu MATCHES "^(aarch64|arm64)$")
  set(TCC_ARCH "arm64")
elseif(_tcc_cpu MATCHES "^(i[3-6]86|x86)$")
  set(TCC_ARCH "i386")
elseif(_tcc_cpu MATCHES "^arm")
  set(TCC_ARCH "arm")
elseif(_tcc_cpu STREQUAL "riscv64")
  set(TCC_ARCH "riscv64")
else()
  message(FATAL_ERROR "tcc: 不支持的 CPU '${CMAKE_SYSTEM_PROCESSOR}'；请显式设置 TCC_ARCH")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(TCC_TARGETOS "Darwin")
elseif(WIN32)
  set(TCC_TARGETOS "WIN32")
else()
  set(TCC_TARGETOS "Linux")
endif()
set(TCC_BUILDOS "${TCC_TARGETOS}")

set(TCC_LIBSUF ".a")
set(TCC_EXESUF "")
if(APPLE)
  set(TCC_DLLSUF ".dylib")
elseif(WIN32)
  set(TCC_DLLSUF ".dll")
else()
  set(TCC_DLLSUF ".so")
endif()

# configure 用的是 `uname -r`；CMake 已经把同一结果放在 CMAKE_SYSTEM_VERSION。
set(TCC_OS_RELEASE "${CMAKE_SYSTEM_VERSION}")
if(NOT TCC_OS_RELEASE)
  set(TCC_OS_RELEASE "unknown")
endif()

execute_process(COMMAND "${TCC_CC}" -print-libgcc-file-name
  OUTPUT_VARIABLE _tcc_libgcc OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_tcc_libgcc AND EXISTS "${_tcc_libgcc}")
  get_filename_component(TCC_LIBGCC "${_tcc_libgcc}" DIRECTORY)
else()
  set(TCC_LIBGCC "")
endif()

set(TCC_TRIPLET "")
if(CMAKE_LIBRARY_ARCHITECTURE
   AND EXISTS "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/crti.o")
  set(TCC_TRIPLET "${CMAKE_LIBRARY_ARCHITECTURE}")
endif()
set(TCC_LIBPATHS "")
set(TCC_CRTPREFIX "")
if(NOT TCC_TRIPLET
   AND TCC_ARCH MATCHES "^(x86_64|arm64|riscv64)$"
   AND EXISTS "/usr/lib64/crti.o")
  set(TCC_LIBPATHS "{B}:/usr/lib64")
  set(TCC_CRTPREFIX "/usr/lib64")
endif()

# ---------------------------------------------------------------------------
# 3. 生成配置内容（只写构建目录，配置阶段对源码树零副作用）
# ---------------------------------------------------------------------------

if(TCC_ARCH STREQUAL "x86_64")
  set(_tcc_target_macro "TCC_TARGET_X86_64")
elseif(TCC_ARCH STREQUAL "i386")
  set(_tcc_target_macro "TCC_TARGET_I386")
elseif(TCC_ARCH STREQUAL "arm64")
  set(_tcc_target_macro "TCC_TARGET_ARM64")
elseif(TCC_ARCH STREQUAL "arm")
  set(_tcc_target_macro "TCC_TARGET_ARM")
else()
  set(_tcc_target_macro "TCC_TARGET_RISCV64")
endif()
set(TCC_TARGET_DEFINES "#define ${_tcc_target_macro} 1")
if(TCC_LIBPATHS)
  string(APPEND TCC_TARGET_DEFINES "\n#define CONFIG_TCC_LIBPATHS \"${TCC_LIBPATHS}\"")
endif()
if(TCC_CRTPREFIX)
  string(APPEND TCC_TARGET_DEFINES "\n#define CONFIG_TCC_CRTPREFIX \"${TCC_CRTPREFIX}\"")
endif()

set(TCC_HOST_DEFINES "")
if(TCC_TRIPLET)
  string(APPEND TCC_HOST_DEFINES "#define CONFIG_TRIPLET \"${TCC_TRIPLET}\"\n")
endif()
string(APPEND TCC_HOST_DEFINES "#define CONFIG_OS_RELEASE \"${TCC_OS_RELEASE}\"\n")
string(APPEND TCC_HOST_DEFINES
  "#ifndef CONFIG_TCCDIR\n#define CONFIG_TCCDIR \"${TCC_INSTALL_TCCDIR}\"\n#endif\n")
string(APPEND TCC_HOST_DEFINES "#define CONFIG_TCC_PREDEFS 1\n")
if(TCC_LIBGCC)
  string(APPEND TCC_HOST_DEFINES "#define CONFIG_TCC_LIBGCC \"${TCC_LIBGCC}\"\n")
endif()
if(TCC_EXTRA_DEFINES)
  string(APPEND TCC_HOST_DEFINES "${TCC_EXTRA_DEFINES}\n")
endif()

set(TCC_CONFIG_VARS "CONFIG_${TCC_ARCH}=yes")
if(TCC_SHARED_LIBTCC)
  string(APPEND TCC_CONFIG_VARS "\nCONFIG_static=no")
endif()
if(TCC_EXTRA_CONFIG_VARS)
  string(REPLACE ";" "\n" _tcc_extra_vars "${TCC_EXTRA_CONFIG_VARS}")
  string(APPEND TCC_CONFIG_VARS "\n${_tcc_extra_vars}")
endif()

file(MAKE_DIRECTORY "${TCC_GENERATED_DIR}")

# 只影响 make 命令行、不进 config.mak 的输入（EXTRA-DEFS / CFLAGS /
# MAKE-ARGS / 额外 CONFIG_*）也必须构成构建签名：改了它们要触发 clean，
# 否则 libtcc.so 会拿另一套宏或优化级别编出来的 .o 去链接。
# 同步脚本拿它和 config.applied 比较；BUILD-DIR 只是诊断信息。
string(REPLACE ";" "," _tcc_make_args_sig "${TCC_MAKE_ARGS}")
string(REPLACE ";" "," _tcc_config_vars_sig "${TCC_EXTRA_CONFIG_VARS}")
string(CONCAT _tcc_signature
  "EXTRA-DEFS=${TCC_EXTRA_DEFS}\n"
  "CFLAGS=${TCC_BUILD_CFLAGS}\n"
  "MAKE-ARGS=${_tcc_make_args_sig}\n"
  "CONFIG-VARS=${_tcc_config_vars_sig}\n"
  "BUILD-DIR=${CMAKE_CURRENT_BINARY_DIR}\n")
set(_tcc_signature_old "")
if(EXISTS "${TCC_GENERATED_DIR}/config.sig")
  file(READ "${TCC_GENERATED_DIR}/config.sig" _tcc_signature_old)
endif()
if(NOT _tcc_signature_old STREQUAL _tcc_signature)
  file(WRITE "${TCC_GENERATED_DIR}/config.sig" "${_tcc_signature}")
endif()

# 配置阶段只读地检查源码树里残留的 owner 标记：tcc/Makefile 的产物落在
# 源码树，多个构建目录交替构建会互相覆盖，尽早给出明确提示（零写入）。
if(EXISTS "${TCC_SOURCE_DIR}/config.mak")
  file(STRINGS "${TCC_SOURCE_DIR}/config.mak" _tcc_owner
       REGEX "^# c6-build-dir: ")
  if(_tcc_owner)
    string(REGEX REPLACE "^# c6-build-dir: " "" _tcc_owner "${_tcc_owner}")
    if(NOT _tcc_owner STREQUAL CMAKE_CURRENT_BINARY_DIR)
      message(WARNING
        "tcc: 源码树里的配置来自另一个构建目录：${_tcc_owner}\n"
        "  当前构建目录：${CMAKE_CURRENT_BINARY_DIR}\n"
        "  tcc/Makefile 的产物落在源码树，交替构建会互相覆盖；请只保留一个"
        "构建目录，或先执行 distclean。")
    endif()
  endif()
endif()

# configure_file 自带「内容不变就不重写」；config.texi 只有一行，手工做
# 同样的比较，避免每次重新 cmake 都无谓地刷新时间戳。
configure_file("${TCC_SOURCE_DIR}/cmake/config.mak.in"
               "${TCC_GENERATED_DIR}/config.mak" @ONLY)
configure_file("${TCC_SOURCE_DIR}/cmake/config.h.in"
               "${TCC_GENERATED_DIR}/config.h" @ONLY)
set(_tcc_texi "@set VERSION ${TCC_VERSION}\n")
set(_tcc_texi_old "")
if(EXISTS "${TCC_GENERATED_DIR}/config.texi")
  file(READ "${TCC_GENERATED_DIR}/config.texi" _tcc_texi_old)
endif()
if(NOT _tcc_texi_old STREQUAL _tcc_texi)
  file(WRITE "${TCC_GENERATED_DIR}/config.texi" "${_tcc_texi}")
endif()

# 构建期同步不能用 per-build-dir 的 stamp：tcc/Makefile 的产物落在源码树，
# 另一个构建目录随时可能覆盖 config.mak，而本目录的 stamp 仍是「新的」，
# 于是静默拿别的配置编译（典型症状：静态构建却产出 libtcc.so）。
# 改成每次构建都做一次内容比对：只有真正变化才 copy + clean。
# 脚本生成在构建目录，不额外往源码树写文件。
set(_tcc_sync_template [==[
# 由 tcc/CMakeLists.txt 生成，cmake -P 执行；每次构建由 tcc-config 调用。
set(_tcc_src "@TCC_SOURCE_DIR@")
set(_tcc_gen "@TCC_GENERATED_DIR@")
set(_tcc_make "@TCC_MAKE_EXECUTABLE@")
set(_tcc_cmake "@CMAKE_COMMAND@")
set(_tcc_clean FALSE)

# 比较时剥掉 owner 注释：同配置的多个构建目录只需更新 owner，不必 clean；
# 功能配置（如 CONFIG_static）真的变了才清旧目标文件。
function(_tcc_functional content out)
  string(REGEX REPLACE "# c6-build-dir: [^\n]*\n" "" _s "${content}")
  set(${out} "${_s}" PARENT_SCOPE)
endfunction()

foreach(_tcc_name IN ITEMS config.mak config.h config.texi)
  set(_tcc_from "${_tcc_gen}/${_tcc_name}")
  set(_tcc_to "${_tcc_src}/${_tcc_name}")
  set(_tcc_new "")
  set(_tcc_old "")
  file(READ "${_tcc_from}" _tcc_new)
  if(EXISTS "${_tcc_to}")
    file(READ "${_tcc_to}" _tcc_old)
  endif()
  _tcc_functional("${_tcc_new}" _tcc_new_key)
  _tcc_functional("${_tcc_old}" _tcc_old_key)
  if(NOT _tcc_new_key STREQUAL _tcc_old_key)
    set(_tcc_clean TRUE)
  endif()
  if(NOT _tcc_new STREQUAL _tcc_old)
    execute_process(COMMAND "${_tcc_cmake}" -E copy_if_different
      "${_tcc_from}" "${_tcc_to}" RESULT_VARIABLE _tcc_r)
    if(NOT _tcc_r EQUAL 0)
      message(FATAL_ERROR "tcc: 同步 ${_tcc_name} 失败 (${_tcc_r})")
    endif()
    message(STATUS "tcc: 同步 ${_tcc_name}")
  endif()
endforeach()

# EXTRA-DEFS / CFLAGS / 额外 make 变量不进 config.mak，用 config.sig 单独
# 记录「上次成功应用的签名」，变化也要触发清理。
set(_tcc_sig_new "")
set(_tcc_sig_old "")
if(EXISTS "${_tcc_gen}/config.sig")
  file(READ "${_tcc_gen}/config.sig" _tcc_sig_new)
endif()
if(EXISTS "${_tcc_gen}/config.applied")
  file(READ "${_tcc_gen}/config.applied" _tcc_sig_old)
endif()
if(NOT _tcc_sig_new STREQUAL _tcc_sig_old)
  set(_tcc_clean TRUE)
endif()

if(_tcc_clean)
  message(STATUS "tcc: 配置变化，清理旧目标文件")
  execute_process(COMMAND "${_tcc_make}" -C "${_tcc_src}" clean
    RESULT_VARIABLE _tcc_r)
  if(NOT _tcc_r EQUAL 0)
    message(FATAL_ERROR "tcc: make clean 失败 (${_tcc_r})")
  endif()
endif()
file(WRITE "${_tcc_gen}/config.applied" "${_tcc_sig_new}")
]==])
string(CONFIGURE "${_tcc_sync_template}" _tcc_sync_script @ONLY)
set(TCC_SYNC_SCRIPT "${TCC_GENERATED_DIR}/tcc-sync-config.cmake")
set(_tcc_sync_old "")
if(EXISTS "${TCC_SYNC_SCRIPT}")
  file(READ "${TCC_SYNC_SCRIPT}" _tcc_sync_old)
endif()
if(NOT _tcc_sync_old STREQUAL _tcc_sync_script)
  file(WRITE "${TCC_SYNC_SCRIPT}" "${_tcc_sync_script}")
endif()

add_custom_target(tcc-config
  COMMAND "${CMAKE_COMMAND}" -P "${TCC_SYNC_SCRIPT}"
  COMMENT "tcc: 检查并同步 config.mak / config.h / config.texi"
  VERBATIM)

# ---------------------------------------------------------------------------
# 4. 产物信息与 IMPORTED target
# ---------------------------------------------------------------------------

if(TCC_SHARED_LIBTCC)
  set(TCC_STATIC FALSE)
  set(TCC_LIBTCC_FILENAME "libtcc${TCC_DLLSUF}")
  set(TCC_LIBTCC_KIND "SHARED")
else()
  set(TCC_STATIC TRUE)
  set(TCC_LIBTCC_FILENAME "libtcc.a")
  set(TCC_LIBTCC_KIND "STATIC")
endif()
if(TCC_EXTRA_CONFIG_VARS MATCHES "CONFIG_cross=yes")
  set(TCC_CROSS TRUE)
else()
  set(TCC_CROSS FALSE)
endif()
if(APPLE)
  set(TCC_INTERFACE_LINK_LIBS "m;pthread")
  set(TCC_PC_PRIVATE_LIBS "-lm -lpthread")
else()
  set(TCC_INTERFACE_LINK_LIBS "m;dl;pthread")
  set(TCC_PC_PRIVATE_LIBS "-lm -ldl -lpthread")
endif()

add_executable(tcc::c6 IMPORTED GLOBAL)
set_target_properties(tcc::c6 PROPERTIES
  IMPORTED_LOCATION "${TCC_OUTPUT_DIR}/c6${TCC_EXESUF}")

# 静态 / 共享在配置期就已确定，用精确的 IMPORTED 类型而不是 UNKNOWN，
# 消费方才能拿到正确的链接语义（PIC、RPATH、传递依赖）。
add_library(tcc::libtcc ${TCC_LIBTCC_KIND} IMPORTED GLOBAL)
set_target_properties(tcc::libtcc PROPERTIES
  IMPORTED_LOCATION "${TCC_OUTPUT_DIR}/${TCC_LIBTCC_FILENAME}"
  INTERFACE_INCLUDE_DIRECTORIES "${TCC_SOURCE_DIR}"
  INTERFACE_LINK_LIBRARIES "${TCC_INTERFACE_LINK_LIBS}")

# ---------------------------------------------------------------------------
# 5. 构建 / 清理目标
# ---------------------------------------------------------------------------

# Makefile 生成器下用 $(MAKE) 继承 CMake 的 jobserver；其它生成器没有
# jobserver，退回显式 -j。这样 `cmake --build build --parallel N` 的并发度
# 能一路传到子 make，而不是被硬编码的 -j 覆盖。
if(CMAKE_GENERATOR MATCHES "Make")
  set(_tcc_make_invocation "\$(MAKE)" -C "${TCC_SOURCE_DIR}")
else()
  set(_tcc_make_invocation "${TCC_MAKE_EXECUTABLE}" -C "${TCC_SOURCE_DIR}" "-j${TCC_JOBS}")
endif()

set(_tcc_make_ext ${_tcc_make_invocation})
if(TCC_EXTRA_DEFS)
  list(APPEND _tcc_make_ext "EXTRA-DEFS=${TCC_EXTRA_DEFS}")
endif()
if(TCC_BUILD_CFLAGS)
  list(APPEND _tcc_make_ext "CFLAGS=${TCC_BUILD_CFLAGS}")
endif()
if(TCC_MAKE_ARGS)
  list(APPEND _tcc_make_ext ${TCC_MAKE_ARGS})
endif()
set(_tcc_make_plain ${_tcc_make_invocation})
if(TCC_BUILD_CFLAGS)
  list(APPEND _tcc_make_plain "CFLAGS=${TCC_BUILD_CFLAGS}")
endif()
if(TCC_MAKE_ARGS)
  list(APPEND _tcc_make_plain ${TCC_MAKE_ARGS})
endif()

add_custom_target(tcc-build
  COMMAND ${_tcc_make_ext}
  WORKING_DIRECTORY "${TCC_SOURCE_DIR}"
  COMMENT "tcc: 构建 c6 / ${TCC_LIBTCC_FILENAME} / libtcc1.a（EXTRA-DEFS=${TCC_EXTRA_DEFS}）"
  VERBATIM USES_TERMINAL)
add_dependencies(tcc-build tcc-config)

add_custom_target(tcc-build-plain
  COMMAND ${_tcc_make_plain}
  WORKING_DIRECTORY "${TCC_SOURCE_DIR}"
  COMMENT "tcc: 上游模式构建（不带 EXTRA-DEFS）"
  VERBATIM USES_TERMINAL)
add_dependencies(tcc-build-plain tcc-config)

add_custom_target(tcc-clean
  COMMAND "${TCC_MAKE_EXECUTABLE}" -C "${TCC_SOURCE_DIR}" clean
  WORKING_DIRECTORY "${TCC_SOURCE_DIR}" VERBATIM USES_TERMINAL)
add_custom_target(tcc-distclean
  COMMAND "${TCC_MAKE_EXECUTABLE}" -C "${TCC_SOURCE_DIR}" distclean
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${TCC_GENERATED_DIR}/config.applied"
  WORKING_DIRECTORY "${TCC_SOURCE_DIR}"
  COMMENT "tcc: 清理构建产物与生成的配置（需重新 cmake 配置）"
  VERBATIM USES_TERMINAL)

# ---------------------------------------------------------------------------
# 6. 导出给父项目：目标靠 IMPORTED target，其余只保留少量语义变量
# ---------------------------------------------------------------------------

# 产物路径一律通过 tcc::c6 / tcc::libtcc 暴露；这里只把父项目后续模块确实
# 要读取的少量语义变量传上去，不再导出整个 TCC_* 命名空间。
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  foreach(_v IN ITEMS
      TCC_VERSION TCC_LIBTCC_FILENAME TCC_LIBTCC_KIND TCC_STATIC TCC_CROSS
      TCC_DLLSUF TCC_EXESUF TCC_INTERFACE_LINK_LIBS TCC_PC_PRIVATE_LIBS)
    set(${_v} "${${_v}}" PARENT_SCOPE)
  endforeach()
endif()
