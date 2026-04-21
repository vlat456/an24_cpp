#pragma once

#include <string>
#include <vector>
#include <utility>

struct Connection {
    std::string from;
    std::string to;
    std::vector<std::pair<float,float>> routing_points;
};
