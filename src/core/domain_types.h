#pragma once

#include <cstdint>

/// Domain types for multi-domain simulation (bitmask for multi-domain components)
enum class Domain : uint8_t {
    Electrical = 1 << 0,
    Logical    = 1 << 1,
    Mechanical = 1 << 2,
    Hydraulic  = 1 << 3,
    Thermal    = 1 << 4,
    Pneumatic  = 1 << 5
};

constexpr Domain operator|(Domain a, Domain b) {
    return static_cast<Domain>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr Domain operator&(Domain a, Domain b) {
    return static_cast<Domain>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr bool has_domain(Domain mask, Domain domain) {
    return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(domain)) != 0;
}

/// Port type for validation and AOT optimization
enum class PortType {
    V,
    I,
    Signal,
    Bool,
    RPM,
    Temperature,
    Pressure,
    Position,
    Contextual,
    Any,
};

constexpr Domain domain_for_port_type(PortType t) {
    switch (t) {
        case PortType::V:
        case PortType::I:
        case PortType::Contextual:
        case PortType::Any:
            return Domain::Electrical;
        case PortType::Signal:
        case PortType::Bool:
            return Domain::Logical;
        case PortType::RPM:
        case PortType::Position:
            return Domain::Mechanical;
        case PortType::Pressure:
            return Domain::Hydraulic;  // Pressure ports used by hydraulic (default)
        case PortType::Temperature:
            return Domain::Thermal;
    }
    return Domain::Electrical;
}

constexpr PortType port_type_for_domain(Domain d) {
    switch (d) {
        case Domain::Electrical: return PortType::V;
        case Domain::Logical:    return PortType::Bool;
        case Domain::Mechanical: return PortType::RPM;
        case Domain::Hydraulic:  return PortType::Pressure;
        case Domain::Thermal:    return PortType::Temperature;
        case Domain::Pneumatic:  return PortType::Pressure;
    }
    return PortType::Contextual;
}
