# c6c 的编译规则：自举编译器本体、跑分用的测量二进制、嵌入库 libc6c。
#
# 由 cmake/C6C.cmake 顺序 include；这里用的 C6C_DIR / C6C_BIN / C6C_SRCS /
# _c6c_ld_env 等都在那里定义，单独 include 本文件没有意义。
#
# 引导编译器（stage0）两条路：
#   C6_ENABLE_TCC=ON  → 仓库自己的 tcc/c6，`-B tcc` 带 crt / libtcc1.a；
#   C6_ENABLE_TCC=OFF → 宿主 C 编译器 `-x c`，不需要任何 tcc 构建产物。

if(C6_ENABLE_TCC)
  set(C6C_STAGE0 "${C6_TCC_DIR}/c6${C6_TCC_EXESUF}")
  add_custom_command(
    OUTPUT "${C6C_BIN}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${C6C_BUILD}"
    COMMAND ${CMAKE_COMMAND} -E env ${_c6c_ld_env}
            "${C6C_STAGE0}" -B "${C6_TCC_DIR}" -g -o "${C6C_BIN}"
            "${C6C_DIR}/src/c6c.c6"
    # 依赖 tcc-build 这个 **target**（不是文件 tcc/c6）：tcc/c6 是它现场
    # make 出来的，没有 OUTPUT 规则，写成文件会让 make 报
    # "No rule to make target .../tcc/c6"；写成 target 则 CMake 会给**所有**
    # 消费 ${C6C_BIN} 的目标（c6c / c6c-opt / c6c-objcopy / c6c-ar …）都加上
    # 目标级依赖，不会出现"objcopy 先跑、tcc/c6 还没生成"的抢跑。
    DEPENDS ${C6C_SRCS} tcc-build
    COMMENT "c6c: 用 ${C6C_STAGE0} 编出自举编译器 (c6c/build/c6c)"
    VERBATIM)
else()
  set(C6C_STAGE0 "${CMAKE_C_COMPILER}")
  add_custom_command(
    OUTPUT "${C6C_BIN}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${C6C_BUILD}"
    COMMAND "${CMAKE_C_COMPILER}" -x c -O2 -o "${C6C_BIN}" "${C6C_DIR}/src/c6c.c6"
    DEPENDS ${C6C_SRCS}
    COMMENT "c6c: 用 ${CMAKE_C_COMPILER} 编出自举编译器 (c6c/build/c6c，stage0 = 宿主 CC)"
    VERBATIM)
endif()

add_custom_target(c6c ALL DEPENDS "${C6C_BIN}")
if(C6_ENABLE_TCC)
  add_dependencies(c6c tcc-build)
else()
  # 不建 tcc，但 tcc 的**配置文件**还是要生成：build-tcc 把 tcc/*.c 当语料编，
  # 那份源码 `#include "config.h"`。tcc-config 只同步配置头，不碰编译产物。
  add_dependencies(c6c tcc-config)
  add_dependencies(c6 c6c)
  add_dependencies(build c6c)
endif()
set_target_properties(c6c PROPERTIES ADDITIONAL_CLEAN_FILES "${C6C_BIN}")

# 性能口径的测量二进制：gcc -O3（tcc 的代码生成比 gcc 差 4 倍，拿 tcc 编的
# 二进制量性能等于在量 tcc 的寄存器分配）。
add_custom_command(
  OUTPUT "${C6C_OPT}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${C6C_BUILD}"
  COMMAND "${CMAKE_C_COMPILER}" -x c -O3 -o "${C6C_OPT}" "${C6C_DIR}/src/c6c.c6"
  DEPENDS ${C6C_SRCS}
  COMMENT "c6c: 编测量二进制 c6c/build/c6c-opt（${CMAKE_C_COMPILER} -O3）"
  VERBATIM)
add_custom_target(c6c-opt DEPENDS "${C6C_OPT}")
set_target_properties(c6c-opt PROPERTIES ADDITIONAL_CLEAN_FILES "${C6C_OPT}")

# 库形态：同一份源码（src/c6c-lib.c6 = -DC6C_NO_MAIN + #include "c6c.c6"）编成
# libc6c.a / libc6c.so，导出 c6_* 嵌入 API 与 tcc_* 兼容层。它**不是** libtcc：
# 不链接 tcc、不 include tcc 的头，只是把同类能力用 c6c 自己的实现提供出来。
set(_c6c_lib_defs
    "-DC6_DEFAULT_RT=\"${C6C_DIR}/lib/rt.c6\""
    "-DC6_DEFAULT_INC=\"${C6C_DIR}/include\"")
add_custom_command(
  OUTPUT "${C6C_LIB_A}" "${C6C_LIB_SO}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${C6C_BUILD}"
  COMMAND "${CMAKE_C_COMPILER}" -x c -O2 -fPIC ${_c6c_lib_defs}
          -c -o "${C6C_BUILD}/libc6c.o" "${C6C_DIR}/src/c6c-lib.c6"
  COMMAND "${CMAKE_AR}" rcs "${C6C_LIB_A}" "${C6C_BUILD}/libc6c.o"
  COMMAND "${CMAKE_C_COMPILER}" -x c -O2 -fPIC -shared ${_c6c_lib_defs}
          -o "${C6C_LIB_SO}" "${C6C_DIR}/src/c6c-lib.c6"
  DEPENDS ${C6C_SRCS}
  COMMENT "c6c: 编出嵌入库 libc6c.a / libc6c.so（宿主 C 编译器，-DC6C_NO_MAIN）"
  VERBATIM)
add_custom_target(c6c-lib ALL DEPENDS "${C6C_LIB_A}" "${C6C_LIB_SO}")
set_target_properties(c6c-lib PROPERTIES
  ADDITIONAL_CLEAN_FILES "${C6C_LIB_A};${C6C_LIB_SO};${C6C_BUILD}/libc6c.o")

# 去 binutils 阶段 4：c6c 自带的 objcopy 子集（-O binary / -R），
# 用 c6c 自己编译并自动挂 rt.c6。内核构建里 setup.bin / realmode.bin /
# vmlinux.bin 都靠 `objcopy -O binary`，这三条的输出已与 GNU 版逐字节一致。
set(C6C_OBJCOPY "${C6C_BUILD}/objcopy")
add_custom_command(
  OUTPUT "${C6C_OBJCOPY}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${C6C_BUILD}"
  COMMAND "${CMAKE_COMMAND}" -E env C6C_LINK_WITH_RT=1
          "${C6C_BIN}" -o "${C6C_OBJCOPY}" "${C6C_DIR}/src/objcopy.c6"
  DEPENDS "${C6C_BIN}" "${C6C_DIR}/src/objcopy.c6"
  COMMENT "c6c: 用 c6c 自编译 objcopy（去 binutils 阶段 4）"
  VERBATIM)
add_custom_target(c6c-objcopy ALL DEPENDS "${C6C_OBJCOPY}")
set_target_properties(c6c-objcopy PROPERTIES ADDITIONAL_CLEAN_FILES "${C6C_OBJCOPY}")

# 去 binutils 阶段 4：c6c 自带的 ar 子集（thin/regular archive 无符号索引），
# 覆盖内核 791 次 `ar cDPrST` 调用（这些中间归档不直接喂给 ld）。
# 生成的 thin archive 经 GNU `ar t` 验证格式合法、成员列表一致。
set(C6C_AR "${C6C_BUILD}/ar")
add_custom_command(
  OUTPUT "${C6C_AR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${C6C_BUILD}"
  COMMAND "${CMAKE_COMMAND}" -E env C6C_LINK_WITH_RT=1
          "${C6C_BIN}" -o "${C6C_AR}" "${C6C_DIR}/src/artool.c6"
  DEPENDS "${C6C_BIN}" "${C6C_DIR}/src/artool.c6"
  COMMENT "c6c: 用 c6c 自编译 ar（去 binutils 阶段 4）"
  VERBATIM)
add_custom_target(c6c-ar ALL DEPENDS "${C6C_AR}")
set_target_properties(c6c-ar PROPERTIES ADDITIONAL_CLEAN_FILES "${C6C_AR}")
