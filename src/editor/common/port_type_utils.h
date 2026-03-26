#pragma once

#include "json_parser/json_parser.h"

namespace editor::common {

inline Domain domain_for_port_type(PortType t) {
    switch (t) {
        case PortType::V:
        case PortType::I:
        case PortType::Any:
            return Domain::Electrical;
        case PortType::Bool:
            return Domain::Logical;
        case PortType::RPM:
        case PortType::Position:
            return Domain::Mechanical;
        case PortType::Pressure:
            return Domain::Hydraulic;
        case PortType::Temperature:
            return Domain::Thermal;
    }
    return Domain::Electrical;
}

inline PortType port_type_for_domain(Domain d) {
    switch (d) {
        case Domain::Electrical: return PortType::V;
        case Domain::Logical: return PortType::Bool;
        case Domain::Mechanical: return PortType::RPM;
        case Domain::Hydraulic: return PortType::Pressure;
        case Domain::Thermal: return PortType::Temperature;
    }
    return PortType::Any;
}

} // namespace editor::common
