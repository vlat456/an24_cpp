# WASM Bridge Module — MSFS 2024
#
# Builds an24_bridge.wasm for deployment inside MSFS 2024.
# Requires: MSFS 2024 WASM SDK, emscripten toolchain.
#
# This target is NOT built on macOS by default. Enable with:
#   cmake -B build -DENABLE_WASM_BRIDGE=ON -DCMAKE_TOOLCHAIN_FILE=<emscripten.cmake>

option(ENABLE_WASM_BRIDGE "Build WASM bridge module for MSFS 2024" OFF)

if(NOT ENABLE_WASM_BRIDGE)
    message(STATUS "WASM bridge disabled (set ENABLE_WASM_BRIDGE=ON to build)")
    return()
endif()

add_executable(an24_bridge
    an24_bridge.cpp
    an24_bridge.h
    bridge_protocol.h
)

# Project include path — for simconnect/wire_protocol.h, simconnect/wire_codec.h
target_include_directories(an24_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
)

# MSFS 2024 WASM SDK include path
# Set via environment or CMake variable:
#   -DMSFS_WASM_SDK_PATH=/path/to/MSFS2024_SDK/WASM
if(DEFINED MSFS_WASM_SDK_PATH)
    target_include_directories(an24_bridge PRIVATE ${MSFS_WASM_SDK_PATH}/include)
elseif(DEFINED ENV{MSFS_WASM_SDK_PATH})
    target_include_directories(an24_bridge PRIVATE $ENV{MSFS_WASM_SDK_PATH}/include)
endif()

# WASM-specific compile flags
target_compile_options(an24_bridge PRIVATE
    -O3
    -flto
    -fno-exceptions
    -fno-rtti
)

target_link_options(an24_bridge PRIVATE
    -O3
    -flto
    --no-entry
    -sEXPORTED_FUNCTIONS=[]  # MSFS finds callbacks by name at runtime
    -sALLOW_MEMORY_GROWTH=1
)

# MSFS_WasmVersions.a — required for MSFS 2024 WASM detection
if(DEFINED MSFS_WASM_SDK_PATH)
    target_link_libraries(an24_bridge PRIVATE
        ${MSFS_WASM_SDK_PATH}/lib/MSFS_WasmVersions.a
    )
endif()

# =============================================================================
# SimVars Catalog Data — bundled AVar names (optional)
# =============================================================================

# The simvar_catalog.json is a JSON file listing bundled AVar names/descriptions
# for the SimVarCatalog on the host side. It's not WASM-specific but is co-located
# here since it describes the variables the bridge reads.
#
# Install target (host-side only):
# install(FILES simvar_catalog.json DESTINATION share/an24/)

message(STATUS "WASM bridge sources: an24_bridge.cpp an24_bridge.h bridge_protocol.h")
