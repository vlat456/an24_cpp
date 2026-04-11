#pragma once

#include "json_parser/json_parser.h"

#include <string>

inline std::string domain_to_string(Domain d) {
    switch (d) {
        case Domain::Electrical: return "Electrical";
        case Domain::Logical: return "Logical";
        case Domain::Mechanical: return "Mechanical";
        case Domain::Hydraulic: return "Hydraulic";
        case Domain::Thermal: return "Thermal";
    }
    return "Unknown";
}
