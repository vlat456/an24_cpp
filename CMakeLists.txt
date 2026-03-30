cmake_minimum_required(VERSION 3.16)
project(an24_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Aggressive optimization flags for Release
# NOTE: Do NOT use -ffast-math here. It implies -ffinite-math-only which makes
# std::isfinite/std::isinf/std::isnan unreliable (always returns true/false)
# and makes std::numeric_limits<float>::infinity() produce UB.
# Instead, use the safe subset of fast-math optimizations:
#   -fno-math-errno        — skip errno writes for math functions
#   -freciprocal-math      — allow 1/x reciprocal optimizations
#   -fassociative-math     — allow reassociation of FP operations
#   -fno-trapping-math     — assume no FP traps
#   -fno-signed-zeros      — treat +0.0 == -0.0
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -fno-math-errno -freciprocal-math -fassociative-math -fno-trapping-math -fno-signed-zeros")

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)

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
