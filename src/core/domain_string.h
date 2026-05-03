#pragma once

#include "core/domain_types.h"

#include <string>

inline std::string domain_to_string(const Domain d) {
    switch (d) {
        case Domain::Electrical: return "Electrical";
        case Domain::Logical: return "Logical";
        case Domain::Mechanical: return "Mechanical";
        case Domain::Hydraulic: return "Hydraulic";
        case Domain::Thermal: return "Thermal";
        case Domain::Pneumatic: return "Pneumatic";
    }
    return "Unknown";
}
