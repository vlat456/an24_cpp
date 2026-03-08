#pragma once

#include "data/port.h"
#include <string>
#include <vector>

/// Cached port info for Inspector display
struct DisplayPort {
    std::string name;
    PortSide side;
    std::string connection;  // "Battery.v_out" or "[not connected]"
};

/// Cached node info for Inspector display
struct DisplayNode {
    std::string name;
    std::string type_name;
    size_t connection_count = 0;
    std::vector<DisplayPort> ports;
};
