#pragma once

/// Shared helpers for blueprint_v2 tests: make_port, set_iface, count helpers.

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "json_parser/json_parser.h"
#include "ui/core/interned_id.h"
#include <cassert>
#include <initializer_list>
#include <string>
#include <vector>

// ==============================================================================
// Port construction
// ==============================================================================

/// Create a PortDescriptor with explicit domain.
inline bp2::PortDescriptor make_port(
    ui::StringInterner& interner,
    const std::string& name,
    Domain domain,
    bp2::Direction direction,
    PortType port_type) {
    return bp2::PortDescriptor{
        interner.intern(name),
        domain,
        direction,
        port_type
    };
}

/// Create a PortDescriptor; domain is derived from port_type.
inline bp2::PortDescriptor make_port(
    ui::InternedId name,
    bp2::Direction direction,
    PortType type) {
    return bp2::PortDescriptor{
        name,
        domain_for_port_type(type),
        direction,
        type
    };
}

/// Convenience overload that interns the name and derives domain.
inline bp2::PortDescriptor make_port(
    ui::StringInterner& interner,
    const char* name,
    bp2::Direction direction,
    PortType type) {
    return make_port(interner.intern(name), direction, type);
}

// ==============================================================================
// Interface helpers
// ==============================================================================

/// Set interface on a component node from a vector of port descriptors.
inline void set_iface(bp2::Blueprint::Node& node, std::vector<bp2::PortDescriptor> ports) {
    assert(node.is_component() && "set_iface requires a component node");
    node.component().iface = bp2::Interface(std::move(ports));
}

/// Set interface on a component node from an initializer list of port descriptors.
inline void set_iface(bp2::Blueprint::Node& node,
                      std::initializer_list<bp2::PortDescriptor> ports) {
    set_iface(node, std::vector<bp2::PortDescriptor>(ports));
}

// ==============================================================================
// Counting helpers
// ==============================================================================

inline size_t count_inputs(const bp2::Interface& iface) {
    size_t count = 0;
    for (const auto& port : iface.ports()) {
        if (port.direction == bp2::Direction::Input) count++;
    }
    return count;
}

inline size_t count_outputs(const bp2::Interface& iface) {
    size_t count = 0;
    for (const auto& port : iface.ports()) {
        if (port.direction == bp2::Direction::Output) count++;
    }
    return count;
}

// ==============================================================================
// Port lookup by index
// ==============================================================================

inline bp2::PortDescriptor const* get_input_port(const bp2::Interface& iface, size_t index) {
    size_t count = 0;
    for (const auto& port : iface.ports()) {
        if (port.direction == bp2::Direction::Input) {
            if (count == index) return &port;
            count++;
        }
    }
    return nullptr;
}

inline bp2::PortDescriptor const* get_output_port(const bp2::Interface& iface, size_t index) {
    size_t count = 0;
    for (const auto& port : iface.ports()) {
        if (port.direction == bp2::Direction::Output) {
            if (count == index) return &port;
            count++;
        }
    }
    return nullptr;
}

inline PortType get_input_type(const bp2::Interface& iface, size_t index) {
    const auto* port = get_input_port(iface, index);
    return port ? port->port_type : PortType::Any;
}

inline PortType get_output_type(const bp2::Interface& iface, size_t index) {
    const auto* port = get_output_port(iface, index);
    return port ? port->port_type : PortType::Any;
}
