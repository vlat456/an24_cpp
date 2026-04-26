#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <variant>

#include "core/model/port.h"
#include "core/model/component_types.h"
#include "core/model/connection.h"
#include "core/model/device_instance.h"

struct ComponentMeta {
    std::string classname;
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, ParamSpec> params;
    std::vector<Domain> domains;
    std::string priority = "med";
    bool critical = false;
};

/// Solver-specific metadata that only applies to primitive components.
/// Composites never have solver traits — they decompose into primitives.
struct PrimitiveSolverMetadata {
    std::optional<SolverRole> solver_role;
    bool scheduler_source = false;
    bool solver_owned_electrical = false;
};

struct PrimitiveSpec : ComponentMeta {
    PrimitiveSolverMetadata solver;
};

struct CompositeSpec : ComponentMeta {
    std::vector<DeviceInstance> devices;
    std::vector<RoutedConnection> connections;
    std::vector<BridgePortDefinition> bridge_ports;
    std::vector<SubBlueprintRef> sub_blueprints;
};

using ComponentSpec = std::variant<PrimitiveSpec, CompositeSpec>;

inline const ComponentMeta& spec_meta(const ComponentSpec& s) {
    return std::visit([](const auto& v) -> const ComponentMeta& { return v; }, s);
}

inline ComponentMeta& spec_meta_mut(ComponentSpec& s) {
    return std::visit([](auto& v) -> ComponentMeta& { return v; }, s);
}

inline const std::string& spec_classname(const ComponentSpec& s) {
    return spec_meta(s).classname;
}

inline const std::unordered_map<std::string, Port>& spec_ports(const ComponentSpec& s) {
    return spec_meta(s).ports;
}

inline std::unordered_map<std::string, Port>& spec_ports_mut(ComponentSpec& s) {
    return spec_meta_mut(s).ports;
}

inline const std::unordered_map<std::string, ParamSpec>& spec_params(const ComponentSpec& s) {
    return spec_meta(s).params;
}

inline std::unordered_map<std::string, ParamSpec>& spec_params_mut(ComponentSpec& s) {
    return spec_meta_mut(s).params;
}

inline const std::vector<Domain>& spec_domains(const ComponentSpec& s) {
    return spec_meta(s).domains;
}

inline std::optional<SolverRole> spec_solver_role(const ComponentSpec& s) {
    if (auto* p = std::get_if<PrimitiveSpec>(&s)) return p->solver.solver_role;
    return std::nullopt;
}

inline bool is_primitive(const ComponentSpec& s) { return std::holds_alternative<PrimitiveSpec>(s); }
inline bool is_composite(const ComponentSpec& s) { return std::holds_alternative<CompositeSpec>(s); }
inline const PrimitiveSpec* as_primitive(const ComponentSpec& s) { return std::get_if<PrimitiveSpec>(&s); }
inline const CompositeSpec* as_composite(const ComponentSpec& s) { return std::get_if<CompositeSpec>(&s); }
inline PrimitiveSpec* as_primitive_mut(ComponentSpec& s) { return std::get_if<PrimitiveSpec>(&s); }
inline CompositeSpec* as_composite_mut(ComponentSpec& s) { return std::get_if<CompositeSpec>(&s); }
