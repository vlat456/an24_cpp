#pragma once

#include "blueprint_v2/blueprint/node_port.h"
#include <string>
#include <vector>

/// Cached port info for Inspector display
struct DisplayPort {
    std::string name;
    bp2::Direction direction;
    std::string connection;  // "Battery.v_out" or "[not connected]"
};

/// Cached node info for Inspector display
struct DisplayNode {
    std::string node_id;  // Blueprint node ID (for selection)
    std::string name;
    std::string type_name;
    size_t connection_count = 0;
    std::vector<DisplayPort> ports;
};
