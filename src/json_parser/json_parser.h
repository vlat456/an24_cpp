#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <nlohmann/json.hpp>

#include "core/domain_types.h"
#include "blueprint_v2/interface/direction.h"
#include "editor/presentation_spec.h"

// Forward declarations
struct DeviceInstance;

/// Port direction — re-exported from direction.h for convenience.
using bp2::Direction;

/// Single port definition
struct Port {
    bp2::Direction direction = bp2::Direction::Output;
    PortType type = PortType::Any;
    Domain domain = Domain::Electrical;
    bool source_writer = false;
    std::optional<std::string> alias;

    Port() = default;
    Port(bp2::Direction direction_)
        : direction(direction_), type(PortType::Any), domain(Domain::Electrical), source_writer(false), alias(std::nullopt) {}
    Port(bp2::Direction direction_, PortType type_, std::optional<std::string> alias_ = std::nullopt)
        : direction(direction_), type(type_), domain(domain_for_port_type(type_)), source_writer(false), alias(std::move(alias_)) {}
    Port(bp2::Direction direction_, PortType type_, Domain domain_, bool source_writer_, std::optional<std::string> alias_ = std::nullopt)
        : direction(direction_), type(type_), domain(domain_), source_writer(source_writer_), alias(std::move(alias_)) {}
};

/// Connection between two ports: "device.port" -> "device.port"
struct Connection {
    std::string from;  // "device.port"
    std::string to;    // "device.port"
    std::vector<std::pair<float,float>> routing_points;  // Editor layout (optional)
};

/// Minimal reference to a sub-blueprint (used in CompositeSpec for library definitions)
struct SubBlueprintRef {
    std::string id;
    std::string blueprint_path;
    std::string type_name;
    std::optional<std::pair<float, float>> pos;
    std::optional<std::pair<float, float>> size;
    std::map<std::string, std::string> params_override;
};

struct BridgePortDefinition {
    std::string id;
    std::string exposed_port;
    bp2::Direction direction = bp2::Direction::Input;
    PortType type = PortType::Contextual;
    std::optional<std::pair<float, float>> pos;
    std::optional<std::pair<float, float>> size;
    std::string label;
};

/// Solver role metadata — describes how a component participates in a domain subsolver.
/// Declared in component blueprints.
struct SolverRole {
    std::string kind;  // "ConductanceBranch", "TheveninSource", "FixedVoltageNode"
    std::unordered_map<std::string, std::string> port_map;   // role key -> port name (e.g. "a" -> "v_in")
    std::unordered_map<std::string, std::string> param_map;  // role key -> param name (e.g. "g" -> "conductance")
    std::unordered_map<std::string, float> value_map;        // role key -> literal value (e.g. "voltage" -> 0.0)
};

/// Explicit execution-phase participation metadata loaded from component JSON.
struct ExecutionPhases {
    bool electrical_passive = false;
    bool electrical_observer = false;
    bool logical = false;
    bool control_commit = false;
    bool electrical_actuator = false;
    bool finalize = false;
    bool mechanical = false;
    bool hydraulic = false;
    bool thermal = false;
};

enum class ParamSchemaType {
    Float,
    Int,
    Bool,
    String,
};

/// Combined parameter specification (default value + schema metadata)
struct ParamSpec {
    ParamSchemaType type = ParamSchemaType::String;
    std::string default_value;
    std::optional<double> min;
    std::optional<double> max;
    bool required = false;
    bool visual_only = false;
};

struct PrimitiveSpec {
    std::string classname;
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, ParamSpec> params;
    std::vector<Domain> domains;
    std::optional<ExecutionPhases> execution;
    bool scheduler_source = false;
    bool solver_owned_electrical = false;
    std::optional<SolverRole> solver_role;
    std::string priority = "med";
    bool critical = false;
    bool visual_only = false;
};

struct CompositeSpec {
    std::string classname;
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, ParamSpec> params;
    std::vector<Domain> domains;
    bool scheduler_source = false;
    bool solver_owned_electrical = false;
    std::string priority = "med";
    bool critical = false;
    bool visual_only = false;
    std::vector<DeviceInstance> devices;
    std::vector<Connection> connections;
    std::vector<BridgePortDefinition> bridge_ports;
    std::vector<SubBlueprintRef> sub_blueprints;
};

using ComponentSpec = std::variant<PrimitiveSpec, CompositeSpec>;

// -- Common accessors --
inline const std::string& spec_classname(const ComponentSpec& s) {
    return std::visit([](const auto& v) -> const std::string& { return v.classname; }, s);
}
inline const std::unordered_map<std::string, Port>& spec_ports(const ComponentSpec& s) {
    return std::visit([](const auto& v) -> const std::unordered_map<std::string, Port>& { return v.ports; }, s);
}
inline std::unordered_map<std::string, Port>& spec_ports_mut(ComponentSpec& s) {
    return std::visit([](auto& v) -> std::unordered_map<std::string, Port>& { return v.ports; }, s);
}
inline const std::unordered_map<std::string, ParamSpec>& spec_params(const ComponentSpec& s) {
    return std::visit([](const auto& v) -> const std::unordered_map<std::string, ParamSpec>& { return v.params; }, s);
}
inline std::unordered_map<std::string, ParamSpec>& spec_params_mut(ComponentSpec& s) {
    return std::visit([](auto& v) -> std::unordered_map<std::string, ParamSpec>& { return v.params; }, s);
}
inline const std::vector<Domain>& spec_domains(const ComponentSpec& s) {
    return std::visit([](const auto& v) -> const std::vector<Domain>& { return v.domains; }, s);
}
inline const std::string& spec_priority(const ComponentSpec& s) {
    return std::visit([](const auto& v) -> const std::string& { return v.priority; }, s);
}
inline bool spec_critical(const ComponentSpec& s) {
    return std::visit([](const auto& v) { return v.critical; }, s);
}
inline bool spec_visual_only(const ComponentSpec& s) {
    return std::visit([](const auto& v) { return v.visual_only; }, s);
}
inline bool spec_scheduler_source(const ComponentSpec& s) {
    return std::visit([](const auto& v) { return v.scheduler_source; }, s);
}
inline bool spec_solver_owned_electrical(const ComponentSpec& s) {
    return std::visit([](const auto& v) { return v.solver_owned_electrical; }, s);
}
inline bool is_primitive(const ComponentSpec& s) { return std::holds_alternative<PrimitiveSpec>(s); }
inline bool is_composite(const ComponentSpec& s) { return std::holds_alternative<CompositeSpec>(s); }
inline const PrimitiveSpec* as_primitive(const ComponentSpec& s) { return std::get_if<PrimitiveSpec>(&s); }
inline const CompositeSpec* as_composite(const ComponentSpec& s) { return std::get_if<CompositeSpec>(&s); }
inline PrimitiveSpec* as_primitive_mut(ComponentSpec& s) { return std::get_if<PrimitiveSpec>(&s); }
inline CompositeSpec* as_composite_mut(ComponentSpec& s) { return std::get_if<CompositeSpec>(&s); }

/// Tree structure mirroring library/ subdirectory hierarchy for menu building.
struct MenuTree {
    std::vector<std::string> entries;                        // Classnames at this level (sorted)
    std::unordered_map<std::string, std::string> labels;     // classname -> display label
    std::map<std::string, MenuTree> children;                // Subfolder name -> subtree (sorted by key)
};

struct TypeRegistry {
    std::unordered_map<std::string, ComponentSpec> types;
    std::unordered_map<std::string, std::string> categories;
    PresentationRegistry presentation;

    const ComponentSpec* get(const std::string& classname) const {
        auto it = types.find(classname);
        if (it != types.end()) return &it->second;
        return nullptr;
    }

    bool has(const std::string& classname) const {
        return types.count(classname) > 0;
    }

    std::vector<std::string> list_classnames() const {
        std::vector<std::string> names;
        names.reserve(types.size());
        for (const auto& [name, _] : types) names.push_back(name);
        return names;
    }

    MenuTree build_menu_tree() const;
    std::optional<std::string> validate_instance(const DeviceInstance& instance) const;
    std::vector<std::string> get_composites_topo_sorted() const;
};

/// Device instance at any level (primitive or composite)
struct DeviceInstance {
    std::string name;
    std::string template_name;  // template used to instantiate this device
    std::string classname;      // component class name (e.g., "Battery")
    std::string display_name;   // user-visible name (from FlatNode::display_name, empty = same as name)
    std::string priority = "med";  // high, med, low
    std::optional<size_t> bucket;  // computation bucket
    bool critical = false;
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, std::string> params;
    std::vector<Domain> domains;  // From component definition only, NOT user-configurable
    bool visual_only = false;      // True = no simulation behavior (e.g. Group)
    std::optional<std::pair<float,float>> pos;   // Editor layout position (optional)
    std::optional<std::pair<float,float>> size;  // Editor layout size (optional)
    std::optional<ExecutionPhases> execution;    // Copied from type definition
    bool scheduler_source = false;               // Copied from type definition
    bool solver_owned_electrical = false;        // Copied from type definition
    std::optional<SolverRole> solver_role;        // Copied from type definition

    // Default constructor
    DeviceInstance() = default;

    DeviceInstance(
        const std::string& name_,
        const std::string& classname_,
        std::unordered_map<std::string, std::string> params_ = {},
        std::unordered_map<std::string, bp2::Direction> ports_ = {}
    ) : name(name_), classname(classname_), params(std::move(params_)) {
        for (const auto& [port_name, direction] : ports_) {
            PortType type = PortType::Any;
            if (port_name.find('v') != std::string::npos) type = PortType::V;
            else if (port_name.find('i') != std::string::npos) type = PortType::I;
            else if (port_name.find("rpm") != std::string::npos) type = PortType::RPM;
            ports[port_name] = Port{direction, type, domain_for_port_type(type), false, std::nullopt};
        }
    }

    DeviceInstance(
        const std::string& name_,
        const std::string& classname_,
        std::unordered_map<std::string, std::string> params_,
        std::unordered_map<std::string, std::string> ports_
    ) : name(name_), classname(classname_), params(std::move(params_)) {
        for (const auto& [port_name, dir_str] : ports_) {
            bp2::Direction dir = (dir_str == "in" || dir_str == "i" || dir_str == "input") ? bp2::Direction::Input : bp2::Direction::Output;
            PortType type = PortType::Any;
            if (port_name.find('v') != std::string::npos) type = PortType::V;
            else if (port_name.find('i') != std::string::npos) type = PortType::I;
            else if (port_name.find("rpm") != std::string::npos) type = PortType::RPM;
            ports[port_name] = Port{dir, type, domain_for_port_type(type), false, std::nullopt};
        }
    }

    /// Get domains for this device
    std::vector<Domain> get_domains() const {
        if (domains.empty()) {
            throw std::runtime_error(
                "Device '" + name + "' (" + classname + ") has no domains. "
                "Type definition should have domains.");
        }
        return domains;
    }
};

/// Subsystem call (template instantiation)
struct SubsystemCall {
    std::string name;
    std::string template_name;
    std::unordered_map<std::string, std::string> port_map;  // external -> internal
};

/// System template (reusable blueprint)
struct SystemTemplate {
    std::string name;
    std::vector<DeviceInstance> devices;
    std::vector<SubsystemCall> subsystems;
    std::unordered_map<std::string, std::string> exposed_ports;  // external -> internal
    std::vector<Domain> domains;  // From component definition
};

/// Compilation context - holds all parsed data
struct ParserContext {
    TypeRegistry registry;               // Type registry
    std::unordered_map<std::string, SystemTemplate> templates;
    std::vector<DeviceInstance> devices;
    std::vector<Connection> connections;
    std::vector<BridgePortDefinition> bridge_ports;
    std::unordered_map<std::string, float> initial_values;

    /// Find device by name
    const DeviceInstance* find_device(const std::string& name) const {
        for (const auto& dev : devices) {
            if (dev.name == name) {
                return &dev;
            }
        }
        return nullptr;
    }

    /// Get template by name
    const SystemTemplate* get_template(const std::string& name) const {
        auto it = templates.find(name);
        if (it != templates.end()) {
            return &it->second;
        }
        return nullptr;
    }
};

/// Parse JSON text into a ParserContext
ParserContext parse_json(const std::string& json_text);

/// Parse JSON text with explicit library directory (for testing)
ParserContext parse_json(const std::string& json_text, const std::string& library_dir);

/// Extract exposed port metadata from structural bridge definitions.
/// For editor/library use when displaying a composite blueprint boundary.
std::unordered_map<std::string, Port> extract_exposed_ports(const ComponentSpec& spec);

/// Serialize a ParserContext to pretty-printed JSON
std::string serialize_json(const ParserContext& ctx);

/// Load type registry from library/ directory
TypeRegistry load_type_registry(const std::string& library_dir = "library/");

/// Merge device instance with component spec defaults
DeviceInstance merge_device_instance(
    const DeviceInstance& instance,
    const ComponentSpec& definition
);

/// Parse a ComponentSpec from JSON (helper for testing)
std::pair<ComponentSpec, TypePresentation> parse_type_definition(const nlohmann::json& j);

/// Expand sub_blueprint references into flat devices + connections.
/// Throws std::runtime_error on circular references.
/// loading_stack tracks ancestors for cycle detection — pass empty set at top call.
CompositeSpec expand_sub_blueprint_references(
    const CompositeSpec& td,
    const TypeRegistry& registry,
    std::set<std::string>& loading_stack);
