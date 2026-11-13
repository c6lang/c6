# wasm 目标：直接调用 scripts/ 下的两个脚本。

# wasm：CMake 直接调用 scripts/ 下的两个脚本。LLVMCC / LLVMAR /
# NODE / WASM_TRIPLE 都从 -D 参数注入脚本的环境。
set(_c6_wasm_env "${CMAKE_COMMAND}" -E env)
if(NOT "${C6_LLVMCC}" STREQUAL "")
  list(APPEND _c6_wasm_env "LLVMCC=${C6_LLVMCC}")
endif()
if(NOT "${C6_LLVMAR}" STREQUAL "")
  list(APPEND _c6_wasm_env "LLVMAR=${C6_LLVMAR}")
endif()
if(NOT "${C6_NODE}" STREQUAL "")
  list(APPEND _c6_wasm_env "NODE=${C6_NODE}")
endif()
if(NOT C6_TCC_STATIC)
  set(_c6_wasm_ld "${C6_TCC_DIR}:$ENV{LD_LIBRARY_PATH}")
  string(REGEX REPLACE ":+$" "" _c6_wasm_ld "${_c6_wasm_ld}")
  list(APPEND _c6_wasm_env "LD_LIBRARY_PATH=${_c6_wasm_ld}")
endif()
if(NOT "${C6_WASM_TRIPLE}" STREQUAL "")
  set(_c6_wasm_args "${C6_WASM_TRIPLE}")
else()
  set(_c6_wasm_args "")
endif()

add_custom_target(wasmrt
  COMMAND ${_c6_wasm_env}
          "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-wasm-runtime.sh"
          ${_c6_wasm_args}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "c6: 编 wasm 交叉运行库 tcc/libtcc1-<arch>.a"
  VERBATIM USES_TERMINAL)
add_dependencies(wasmrt c6)
add_custom_target(wasmtest
  COMMAND ${_c6_wasm_env}
          "${CMAKE_CURRENT_SOURCE_DIR}/scripts/wasm-smoke.sh" ${_c6_wasm_args}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "c6: --emit-llvm -> .wasm -> node 端到端冒烟"
  VERBATIM USES_TERMINAL)
add_dependencies(wasmtest c6)
