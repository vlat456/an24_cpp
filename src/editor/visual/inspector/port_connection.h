#pragma once

#include <string>
#include <optional>

/// Result of finding what connects to a port
struct PortConnection {
    std::string node_id;      // "main_battery"
    std::string port_name;    // "v_out"
    std::string display;      // "Battery.v_out"
    size_t wire_index = 0;    // For double-click to select wire
};
