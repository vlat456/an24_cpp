# SimConnect Bridge Module
# Platform-agnostic interface + stub (macOS) / Win32 (Windows) implementation.

set(SIMCONNECT_SOURCES
    simconnect_bridge.cpp
    simconnect_bridge.h
    simconnect_client_stub.cpp
    simconnect_client_stub.h
    simconnect_client.h
    simconnect_mapping.h
)

add_library(simconnect_bridge STATIC ${SIMCONNECT_SOURCES})
target_include_directories(simconnect_bridge PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
)
target_link_libraries(simconnect_bridge PUBLIC
    jit_solver
)
target_link_libraries(simconnect_bridge PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)
