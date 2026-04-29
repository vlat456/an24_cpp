cmake_minimum_required(VERSION 3.16)
project(an24_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Release optimization flags.
# -ffast-math is NOT enabled globally — it breaks NaN propagation used by
# spdlog/fmt diagnostics. AOT-generated code uses #pragma GCC optimize("fast-math")
# in the generated source; the JIT solver does not use -ffast-math at all.
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# AddressSanitizer + UndefinedBehaviorSanitizer for Debug builds.
# Catches memory errors (use-after-free, buffer overflow) and UB
# (signed overflow, null deref, misaligned access) at test time.
# Zero overhead in Release builds.
option(ENABLE_SANITIZERS "Enable ASan+UBSan in Debug builds" ON)

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)

# Sanitizer configuration — Debug builds only, zero Release overhead.
if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Fetch dependencies
include(FetchContent)

# nlohmann/json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

# GoogleTest
if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
endif()

# SDL2
find_package(SDL2 REQUIRED)

# imgui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90
)
FetchContent_MakeAvailable(imgui)

# native-file-dialog-extended (NFDe)
FetchContent_Declare(
    nfde
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG v1.3.0
)
FetchContent_MakeAvailable(nfde)

# Lua 5.4 (for LuaScript component)
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/walterschell/Lua.git
    GIT_TAG v5.4.7
)
FetchContent_MakeAvailable(lua)

# =====================================================================
# Build number generation
# Format: YYYYMMDD+NNNTAG (e.g. 20260429+003DEBUG)
# Counter persisted in build_counter.txt (gitignored), resets daily.
# Only files #including "build_info.h" recompile on number change.
# =====================================================================

string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)
string(TIMESTAMP BUILD_STAMP "%Y%m%d" UTC)

set(COUNTER_FILE "${CMAKE_SOURCE_DIR}/build_counter.txt")
set(COUNTER_STAMP_FILE "${CMAKE_SOURCE_DIR}/build_counter_stamp.txt")

# Read or initialize counter
if(EXISTS "${COUNTER_FILE}")
    file(STRINGS "${COUNTER_FILE}" _COUNTER_RAW LIMIT_COUNT 1)
else()
    set(_COUNTER_RAW "0")
endif()

# Check if date has changed — reset counter on new day
if(EXISTS "${COUNTER_STAMP_FILE}")
    file(STRINGS "${COUNTER_STAMP_FILE}" _COUNTER_STAMP LIMIT_COUNT 1)
    if(NOT "${_COUNTER_STAMP}" STREQUAL "${BUILD_STAMP}")
        set(_COUNTER_RAW "0")
    endif()
endif()

# Increment counter (no upper bound)
math(EXPR BUILD_COUNTER_INT "${_COUNTER_RAW} + 1")

# Zero-pad to 3 digits
if(BUILD_COUNTER_INT LESS 10)
    set(BUILD_COUNTER_PAD "00${BUILD_COUNTER_INT}")
elseif(BUILD_COUNTER_INT LESS 100)
    set(BUILD_COUNTER_PAD "0${BUILD_COUNTER_INT}")
else()
    set(BUILD_COUNTER_PAD "${BUILD_COUNTER_INT}")
endif()

# Persist raw integer (not padded)
file(WRITE "${COUNTER_FILE}" "${BUILD_COUNTER_INT}")
file(WRITE "${COUNTER_STAMP_FILE}" "${BUILD_STAMP}")

# Derive tag
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(BUILD_TAG "DEBUG")
else()
    set(BUILD_TAG "RELEASE")
endif()

string(CONCAT BUILD_NUMBER "${BUILD_STAMP}" "+" "${BUILD_COUNTER_PAD}" "${BUILD_TAG}")

configure_file(
    "${CMAKE_SOURCE_DIR}/generated/build_info.h.in"
    "${CMAKE_BINARY_DIR}/generated/build_info.h"
    @ONLY
)

# Source directories
add_subdirectory(src)
add_subdirectory(examples)
add_subdirectory(tools)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
