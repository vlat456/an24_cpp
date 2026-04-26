#pragma once

/// @file elaboration_parity_fixtures.h
/// Shared test fixtures for JIT/codegen parity tests.
/// Eliminates duplication between test_codegen_export_parity.cpp and
/// test_export_flattener_parity.cpp.

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/path/path.h"
#include "core/solvers/common/signal_key.h"
#include "io/json/component_registry_json_loader.h"
#include "core/strings/interned_id.h"

#include <set>
#include <string>

/// Build a PrimitiveSpec with the given classname, ports, and domains.
inline PrimitiveSpec make_primitive_spec(
    const std::string& classname,
    std::initializer_list<std::pair<const char*, Port>> ports,
    std::vector<Domain> domains)
{
    PrimitiveSpec spec;
    spec.classname = classname;
    spec.domains = std::move(domains);
    for (const auto& [name, port] : ports) {
        spec.ports[name] = port;
    }
    return spec;
}

/// Singleton test registry with Battery, Resistor, LED, InertiaNode.
inline const ComponentRegistry& parity_registry() {
    static const ComponentRegistry registry = [] {
        ComponentRegistry reg;
        reg.register_type("Battery", make_primitive_spec(
            "Battery",
            {
                {"v_out", Port{bp2::Direction::Output, PortType::V, Domain::Electrical, false}},
                {"v_in", Port{bp2::Direction::Input, PortType::V, Domain::Electrical, false}},
            },
            {Domain::Electrical}));
        reg.register_type("Resistor", make_primitive_spec(
            "Resistor",
            {
                {"v_in", Port{bp2::Direction::Input, PortType::V, Domain::Electrical, false}},
                {"v_out", Port{bp2::Direction::Output, PortType::V, Domain::Electrical, false}},
            },
            {Domain::Electrical}));
        reg.register_type("LED", make_primitive_spec(
            "LED",
            {
                {"v_in", Port{bp2::Direction::Input, PortType::V, Domain::Electrical, false}},
            },
            {Domain::Electrical}));
        reg.register_type("InertiaNode", make_primitive_spec(
            "InertiaNode",
            {
                {"rpm_out", Port{bp2::Direction::Output, PortType::RPM, Domain::Mechanical, false}},
            },
            {Domain::Mechanical}));
        return reg;
    }();
    return registry;
}

/// Create a component Blueprint node with the given id, type, and ports.
inline bp2::Blueprint::Node make_node(core::StringInterner& I,
                                const char* id,
                                const char* type,
                                std::initializer_list<bp2::PortDescriptor> ports) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern(type);
    n.component().iface = bp2::Interface(ports);
    return n;
}

/// Create an input PortDescriptor.
inline bp2::PortDescriptor in_port(core::StringInterner& I, const char* name, PortType t = PortType::V) {
    return {I.intern(name), ::domain_for_port_type(t), bp2::Direction::Input, t};
}

/// Create an output PortDescriptor.
inline bp2::PortDescriptor out_port(core::StringInterner& I, const char* name, PortType t = PortType::V) {
    return {I.intern(name), ::domain_for_port_type(t), bp2::Direction::Output, t};
}

/// Create a bridge port Blueprint node.
inline bp2::Blueprint::Node make_bridge_node(core::StringInterner& I,
                                        const char* id,
                                        const char* exposed_port,
                                        bool input_side,
                                        PortType type = PortType::V) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern("BridgePort");
    n.view.name = exposed_port;
    n.content = bp2::Blueprint::Node::BridgePortData{
        I.intern(exposed_port),
        input_side ? bp2::BridgeDirection::Input
                   : bp2::BridgeDirection::Output,
        type,
    };
    return n;
}

/// Collect device names from any input type that has a .devices vector.
inline std::set<std::string> collect_device_names(const auto& input) {
    std::set<std::string> out;
    for (const auto& dev : input.devices) {
        out.insert(dev.name);
    }
    return out;
}
