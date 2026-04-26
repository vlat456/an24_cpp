#pragma once

#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/common/signal_allocation.h"
#include "core/solvers/jit/state.h"
#include "core/model/component_kind.h"
#include "io/json/component_registry_json_loader.h"
#include "core/registry/component_resolution.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include <deque>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>

inline const ComponentRegistry& test_registry() {
    static const ComponentRegistry registry = load_component_registry("library/");
    return registry;
}

/// Copy type specs from the authoritative library registry into a custom registry.
/// Throws on missing spec (unlike ASSERT_NE, which silently returns from void helpers).
inline void register_from_library(ComponentRegistry& registry, std::initializer_list<const char*> classnames) {
    for (const char* name : classnames) {
        const ComponentSpec* spec = test_registry().get(name);
        if (!spec) {
            throw std::runtime_error(std::string("register_from_library: missing library spec: ") + name);
        }
        registry.register_type(name, *spec);
    }
}

/// Construct a BridgePortDefinition for test composite wiring.
inline BridgePortDefinition make_bridge_port_def(const std::string& id,
                                                  bp2::BridgeDirection direction,
                                                  PortType type = PortType::Any,
                                                  const std::string& exposed_port = "") {
    BridgePortDefinition bridge;
    bridge.id = id;
    bridge.exposed_port = exposed_port.empty() ? id : exposed_port;
    bridge.direction = direction;
    bridge.type = type;
    bridge.label = bridge.exposed_port;
    return bridge;
}

inline SimulationState make_state(uint32_t signal_count) {
    SimulationState st;
    for (uint32_t i = 0; i < signal_count; ++i) {
        (void)st.allocate_signal(0.0f);
    }
    return st;
}

inline DeviceInstance make_device_with_ports(
    const std::string& name,
    const std::string& classname,
    const std::unordered_map<std::string, std::string>& params = {},
    const std::vector<std::string>& explicit_ports = {},
    bool merge_defaults = true)
{
    DeviceInstance dev;
    dev.name = name;
    dev.classname = classname;
    dev.params = params;
    const ComponentSpec* def = test_registry().get(classname);

    if (def && explicit_ports.empty()) {
        for (const auto& [port_name, port] : spec_ports(*def)) {
            dev.ports[port_name] = port;
        }
    } else {
        std::vector<std::string> ports = explicit_ports.empty()
            ? get_component_ports(parse_component_kind(classname).value_or(ComponentKind::Unknown))
            : explicit_ports;
        for (const auto& port_name : ports) {
            dev.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
        }
    }

    if (def && merge_defaults) {
        const auto& default_params = spec_params(*def);
        for (const auto& [param_name, param_spec] : default_params) {
            if (param_spec.visual_only) {
                continue;
            }
            if (!dev.params.count(param_name)) {
                dev.params[param_name] = param_spec.default_value;
            }
        }
    }

    return dev;
}

inline DeviceInstance make_device(
    const std::string& name,
    const std::string& classname,
    const std::unordered_map<std::string, std::string>& params = {})
{
    return make_device_with_ports(name, classname, params);
}

inline ResolvedDevice make_resolved_device(
    const std::string& name,
    const std::string& classname,
    const std::unordered_map<std::string, std::string>& params = {})
{
    DeviceInstance dev = make_device(name, classname, params);
    const ComponentSpec* spec = test_registry().get(classname);
    if (!spec) {
        throw std::runtime_error("make_resolved_device missing spec for '" + classname + "'");
    }
    return resolve_component(dev, *spec);
}

inline ResolvedDevice make_resolved_device_with_role(
    const std::string& name,
    const std::string& classname,
    const std::unordered_map<std::string, std::string>& params,
    SolverRole role)
{
    DeviceInstance dev = make_device(name, classname, params);
    const ComponentSpec* base = test_registry().get(classname);

    ResolvedDevice resolved;
    resolved.name = name;
    resolved.classname = classname;
    resolved.kind = parse_component_kind(classname).value_or(ComponentKind::Unknown);
    resolved.params = dev.params;
    resolved.ports = dev.ports;
    resolved.domains = base ? spec_domains(*base) : std::vector<Domain>{Domain::Electrical};
    resolved.priority = base ? spec_meta(*base).priority : "med";
    resolved.solver_role = std::move(role);
    return resolved;
}

inline ResolvedDevice make_raw_resolved_device(
    std::string name,
    std::string classname,
    std::unordered_map<std::string, std::string> params,
    std::unordered_map<std::string, Port> ports)
{
    ResolvedDevice dev;
    dev.name = std::move(name);
    dev.classname = std::move(classname);
    dev.kind = parse_component_kind(dev.classname).value_or(ComponentKind::Unknown);
    dev.params = std::move(params);
    dev.ports = std::move(ports);
    return dev;
}

inline JitBuildInput make_jit_input_from_resolved(
    std::vector<ResolvedDevice> resolved_devices,
    std::unordered_map<std::string, float> initial_values = {})
{
    JitBuildInput input;
    input.initial_values = std::move(initial_values);

    // Signal allocation with sorted port names for deterministic InternedId assignment.
    // Each device's ports are sorted alphabetically (same as build_port_index_map).
    uint32_t next_signal = 0;
    for (const auto& dev : resolved_devices) {
        std::vector<std::string_view> sorted_names;
        sorted_names.reserve(dev.ports.size());
        for (const auto& [port_name, port] : dev.ports) {
            (void)port;
            sorted_names.push_back(port_name);
        }
        std::sort(sorted_names.begin(), sorted_names.end());
        for (const auto& port_name : sorted_names) {
            const std::string full_port = dev.name + "." + std::string(port_name);
            input.port_to_signal[input.signal_key_interner.intern(full_port)] = next_signal++;
        }
    }

    // Convert to solver-facing view after signal mapping.
    input.devices.reserve(resolved_devices.size());
    for (const auto& rd : resolved_devices) {
        input.devices.push_back(to_solver_device(rd));
    }

    input.signal_count = next_signal + 1;
    return input;
}

inline JitBuildInput make_jit_input_resolved(
    std::vector<ResolvedDevice> resolved_devices,
    const std::vector<std::vector<std::string>>& signal_groups,
    std::unordered_map<std::string, float> initial_values = {})
{
    JitBuildInput input;
    input.initial_values = std::move(initial_values);

    // Convert to solver-facing view first (needed for port enumeration below).
    input.devices.reserve(resolved_devices.size());
    for (const auto& rd : resolved_devices) {
        input.devices.push_back(to_solver_device(rd));
    }

    std::unordered_set<std::string> seen_ports;

    for (size_t group_idx = 0; group_idx < signal_groups.size(); ++group_idx) {
        const auto& group = signal_groups[group_idx];
        for (const auto& port : group) {
            if (seen_ports.count(port)) {
                throw std::runtime_error("Duplicate port string across groups: " + port);
            }
            seen_ports.insert(port);
            input.port_to_signal[input.signal_key_interner.intern(port)] = static_cast<uint32_t>(group_idx);
        }
    }

    uint32_t next_signal = static_cast<uint32_t>(signal_groups.size());
    for (const auto& dev : input.devices) {
        std::vector<std::string_view> sorted_names;
        sorted_names.reserve(dev.ports.size());
        for (const auto& [pn, p] : dev.ports) { (void)p; sorted_names.push_back(pn); }
        std::sort(sorted_names.begin(), sorted_names.end());
        for (const auto& port_name : sorted_names) {
            const std::string full_port = dev.name + "." + std::string(port_name);
            if (seen_ports.insert(full_port).second) {
                input.port_to_signal[input.signal_key_interner.intern(full_port)] = next_signal++;
            }
        }
    }

    input.signal_count = next_signal + 1;
    return input;
}

/// Helper to construct JitBuildInput from explicit signal groups.
/// Each group is a vector of port strings that must map to the same signal.
/// Any declared device ports not mentioned in signal_groups are assigned their
/// own singleton signals so raw-builder tests keep the old "all ports exist"
/// behavior without relying on pairwise connection input.
inline JitBuildInput make_jit_input(
    std::vector<DeviceInstance> devices,
    const std::vector<std::vector<std::string>>& signal_groups,
    std::unordered_map<std::string, float> initial_values = {},
    const ComponentRegistry* registry = nullptr)
{
    JitBuildInput input;
    input.initial_values = std::move(initial_values);

    const ComponentRegistry& reg = registry ? *registry : test_registry();

    input.devices.reserve(devices.size());
    for (const auto& dev : devices) {
        const ComponentSpec* spec = reg.get(dev.classname);
        if (spec == nullptr) {
            throw std::runtime_error("make_jit_input missing spec for device '" + dev.name +
                "' (classname: " + dev.classname + ")");
        }
        input.devices.push_back(to_solver_device(resolve_component(dev, *spec)));
    }
    
    std::unordered_set<std::string> seen_ports;
    
    // Assign each explicitly connected net first.
    for (size_t group_idx = 0; group_idx < signal_groups.size(); ++group_idx) {
        const auto& group = signal_groups[group_idx];
        for (const auto& port : group) {
            if (seen_ports.count(port)) {
                throw std::runtime_error("Duplicate port string across groups: " + port);
            }
            seen_ports.insert(port);
            input.port_to_signal[input.signal_key_interner.intern(port)] = static_cast<uint32_t>(group_idx);
        }
    }

    uint32_t next_signal = static_cast<uint32_t>(signal_groups.size());
    for (const auto& dev : input.devices) {
        std::vector<std::string_view> sorted_names;
        sorted_names.reserve(dev.ports.size());
        for (const auto& [pn, p] : dev.ports) { (void)p; sorted_names.push_back(pn); }
        std::sort(sorted_names.begin(), sorted_names.end());
        for (const auto& port_name : sorted_names) {
            const std::string full_port = dev.name + "." + std::string(port_name);
            if (seen_ports.insert(full_port).second) {
                input.port_to_signal[input.signal_key_interner.intern(full_port)] = next_signal++;
            }
        }
    }

    input.signal_count = next_signal + 1;  // +1 for sentinel
    
    return input;
}

/// Helper to construct JitBuildInput from composite signal allocation rules.
/// Uses signal_alloc production functions to compute signal allocation
/// from explicit device and connection pairs.
inline JitBuildInput make_jit_input_from_composite(
    std::vector<DeviceInstance> devices,
    const std::vector<BridgePortDefinition>& bridge_ports,
    const std::vector<Connection>& connections,
    const ComponentRegistry* registry = nullptr)
{
    JitBuildInput input;
    input.bridge_ports = bridge_ports;

    const ComponentRegistry& reg = registry ? *registry : test_registry();

    input.devices.reserve(devices.size());
    for (const auto& dev : devices) {
        const ComponentSpec* spec = reg.get(dev.classname);
        if (spec == nullptr) {
            throw std::runtime_error("make_jit_input_from_composite missing spec for device '" + dev.name +
                "' (classname: " + dev.classname + ")");
        }
        // Skip visual-only devices - same as elaboration boundary filtering
        if (auto* pres = reg.get_presentation(dev.classname)) {
            if (pres->visual_only) {
                continue;
            }
        }
        input.devices.push_back(to_solver_device(resolve_component(dev, *spec)));
    }
    
    // Build port index map from all declared device ports
    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;
    signal_alloc::build_port_index_map(input.devices, bridge_ports, all_ports, port_to_idx);
    
    // Construct union-find for signal allocation
    signal_alloc::UnionFind uf(all_ports.size());
    
    // Apply signal allocation rules (connections, alias rules, etc.)
    signal_alloc::apply_signal_allocation_rules(uf, input.devices, bridge_ports, connections, port_to_idx);
    
    // Finalize signal indices from union-find result
    uint32_t signal_count = 0;
    const auto string_p2s =
        signal_alloc::finalize_signal_indices(uf, all_ports, port_to_idx, signal_count);
    // Convert string-keyed map to InternedId-keyed map (sorted for determinism)
    std::vector<std::string> sorted_keys;
    sorted_keys.reserve(string_p2s.size());
    for (const auto& [ps, sg] : string_p2s) { (void)sg; sorted_keys.emplace_back(ps); }
    std::sort(sorted_keys.begin(), sorted_keys.end());
    for (const auto& port_str : sorted_keys) {
        input.port_to_signal[input.signal_key_interner.intern(port_str)] = string_p2s.at(port_str);
    }

    input.signal_count = signal_count;
    
    return input;
}

// ==============================================================================
// JIT Flattener path — shared by AOT↔JIT equivalence tests
// ==============================================================================

/// Run the full JIT path: registry → BlueprintLibrary → Flattener → elaborate_for_jit → build_systems_dev.
/// Used to compare JIT results against AOT codegen output.
inline BuildResult run_jit_flattener_path(const CompositeSpec& composite, const ComponentRegistry& registry) {
    core::StringInterner interner;
    bp2::BlueprintLibrary library;
    for (const auto& [name, spec] : registry.all_types()) {
        if (is_composite(spec)) {
            auto bp = bp2::blueprint_from_type_definition(spec, interner, registry);
            library.add(interner.intern(name), std::move(bp));
        }
    }
    auto bp = bp2::blueprint_from_type_definition(ComponentSpec{composite}, interner, registry);
    bp2::PathArena arena(interner);
    bp2::Flattener flattener(library);
    auto netlist = flattener.flatten(bp, arena);
    auto input = bp2::elaboration::elaborate_for_jit(netlist, arena, interner, registry);
    return build_systems_dev(input);
}

/// Look up signal index for a port in a BuildResult. Returns UINT32_MAX if not found.
inline uint32_t jit_signal_of(const BuildResult& result, const std::string& port) {
    auto it = result.port_to_signal.find(result.signal_key_interner.lookup(port));
    return it != result.port_to_signal.end() ? it->second : UINT32_MAX;
}

// ==============================================================================
// Shared composite registries
// ==============================================================================

/// Build a registry containing the "voltage_indicator" composite (vin→lamp→vout).
/// Used by AOT↔JIT topology parity tests in multiple test files.
inline ComponentRegistry build_voltage_indicator_registry() {
    ComponentRegistry registry;
    register_from_library(registry, {"IndicatorLight"});

    CompositeSpec lamp;
    lamp.classname = "voltage_indicator";
    DeviceInstance d_lamp;
    d_lamp.name = "lamp";
    d_lamp.classname = "IndicatorLight";
    d_lamp.params["conductance"] = "0.002";  // Required param for ConductanceBranch solver role
    lamp.devices.push_back(d_lamp);
    lamp.bridge_ports = {
        make_bridge_port_def("vin", bp2::BridgeDirection::Input, PortType::V),
        make_bridge_port_def("vout", bp2::BridgeDirection::Output, PortType::V),
    };
    lamp.connections = {
        {"vin.port", "lamp.v_in", {}},
        {"lamp.v_out", "vout.port", {}}
    };
    lamp.ports["vin"]  = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    lamp.ports["vout"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    registry.register_type("voltage_indicator", lamp);

    return registry;
}
