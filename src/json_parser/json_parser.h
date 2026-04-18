#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <nlohmann/json.hpp>

// Forward declarations
struct DeviceInstance;

/// Domain types for multi-domain simulation (bitmask for multi-domain components)
enum class Domain : uint8_t {
    Electrical = 1 << 0,  // 60 Hz - fast electrical dynamics
    Logical    = 1 << 1,  // 60 Hz - boolean logic operations (runs every frame)
    Mechanical = 1 << 2,  // 20 Hz - medium mechanical systems
    Hydraulic  = 1 << 3,  // 5 Hz - slow fluid dynamics
    Thermal    = 1 << 4   // 1 Hz - very slow temperature changes
};

/// Bitwise OR for Domain bitmask
constexpr Domain operator|(Domain a, Domain b) {
    return static_cast<Domain>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

/// Bitwise AND for Domain bitmask
constexpr Domain operator&(Domain a, Domain b) {
    return static_cast<Domain>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

/// Check if domain mask has specific domain
constexpr bool has_domain(Domain mask, Domain domain) {
    return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(domain)) != 0;
}

/// Port type for validation and AOT optimization
enum class PortType {
    V,            // Voltage (electrical potential)
    I,            // Current (electrical flow)
    Signal,       // Scalar logical/control signal
    Bool,         // Boolean (logic level, on/off)
    RPM,          // Rotational speed (revolutions per minute)
    Temperature,  // Temperature (degrees Celsius)
    Pressure,     // Pressure (Pascal, bar, etc.)
    Position,     // Position/Displacement (mechanical position)
    Contextual,   // Concrete type/domain resolved from graph context
    Any,          // Wildcard - can connect to any type
};

/// Canonical mapping from PortType to runtime Domain.
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
            return Domain::Hydraulic;
        case PortType::Temperature:
            return Domain::Thermal;
    }
    return Domain::Electrical;
}

/// Canonical reverse mapping from Domain to a representative PortType.
constexpr PortType port_type_for_domain(Domain d) {
    switch (d) {
        case Domain::Electrical: return PortType::V;
        case Domain::Logical:    return PortType::Bool;
        case Domain::Mechanical: return PortType::RPM;
        case Domain::Hydraulic:  return PortType::Pressure;
        case Domain::Thermal:    return PortType::Temperature;
    }
    return PortType::Contextual;
}

/// Port direction — canonical enum used everywhere.
/// Defined in blueprint_v2/interface/direction.h; re-exported here for
/// backward-compat-free access from parser code.
#include "blueprint_v2/interface/direction.h"

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

/// Minimal reference to a sub-blueprint (used in TypeDefinition for library definitions)
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
    bp2::Direction side = bp2::Direction::Input;
    PortType type = PortType::Contextual;
    std::optional<std::pair<float, float>> pos;
    std::optional<std::pair<float, float>> size;
    std::string label;
};

/// Solver role metadata — describes how a component participates in a domain subsolver.
/// Primitives declare this in their blueprint; wrappers rely on classname fallback.
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

struct ParamSchemaEntry {
    ParamSchemaType type = ParamSchemaType::String;
    std::optional<double> min;
    std::optional<double> max;
    bool required = false;
    bool visual_only = false;   ///< True = editor-only param, excluded from simulation JSON
};

/// Type definition (ports, params, domains for a component class or blueprint)
struct TypeDefinition {
    std::string classname;                    // C++ class name or blueprint classname (e.g., "Battery", "SimpleBattery")
    std::string description;                  // Human-readable description
    bool cpp_class = true;                    // true = C++ component, false = blueprint
    std::unordered_map<std::string, Port> ports;  // Port definitions
    std::unordered_map<std::string, std::string> params;  // Default parameter values
    std::unordered_map<std::string, ParamSchemaEntry> param_schema;  // Explicit typed parameter schema
    std::optional<std::vector<Domain>> domains;    // Domains
    std::string priority = "med";     // Priority
    bool critical = false;            // Critical flag
    std::string content_type = "None"; // UI content type (None, Gauge, Switch, Text)
    std::string render_hint;  // Visual hint for editor rendering ("bus", "ref", or empty)
    bool visual_only = false;  // True = no simulation behavior (e.g. Group)
    std::optional<std::pair<float, float>> size;  // Size in grid units {width, height}
    std::optional<ExecutionPhases> execution;      // Explicit execution-phase metadata
    bool scheduler_source = false;                 // Explicit scheduler source classification
    bool solver_owned_electrical = false;           // Explicit solver ownership for electrical propagation
    std::optional<SolverRole> solver_role;          // Subsolver role metadata (primitives only)
    // For blueprints only: internal devices and connections
    std::vector<DeviceInstance> devices;  // Internal devices (for blueprints)
    std::vector<Connection> connections;  // Internal connections (for blueprints)
    std::vector<BridgePortDefinition> bridge_ports;  // Structural bridge nodes for composites
    // Sub-blueprint references (cpp_class = false composites only)
    std::vector<SubBlueprintRef> sub_blueprints;
};

/// Tree structure mirroring library/ subdirectory hierarchy for menu building.
struct MenuTree {
    std::vector<std::string> entries;                        // Classnames at this level (sorted)
    std::unordered_map<std::string, std::string> labels;     // classname -> display label
    std::map<std::string, MenuTree> children;                // Subfolder name -> subtree (sorted by key)
};

/// Type registry - holds all type definitions loaded from library/
struct TypeRegistry {
    std::unordered_map<std::string, TypeDefinition> types;
    std::unordered_map<std::string, std::string> categories;  // classname → relative subdir path (e.g., "electrical")

    /// Get type definition by classname
    const TypeDefinition* get(const std::string& classname) const {
        auto it = types.find(classname);
        if (it != types.end()) {
            return &it->second;
        }
        return nullptr;
    }

    /// Check if classname exists
    bool has(const std::string& classname) const {
        return types.count(classname) > 0;
    }

    /// Get all registered classnames
    std::vector<std::string> list_classnames() const {
        std::vector<std::string> names;
        names.reserve(types.size());
        for (const auto& [name, _] : types) {
            names.push_back(name);
        }
        return names;
    }

    /// Build a menu tree from directory hierarchy
    MenuTree build_menu_tree() const;

    /// Validate instance against definition
    std::optional<std::string> validate_instance(const DeviceInstance& instance) const;

    /// Get all composites (cpp_class = false) in topological order (leaves first).
    /// Used for AOT codegen to generate nested Systems classes.
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
std::unordered_map<std::string, Port> extract_exposed_ports(const TypeDefinition& blueprint);

/// Serialize a ParserContext to pretty-printed JSON
std::string serialize_json(const ParserContext& ctx);

/// Load type registry from library/ directory
TypeRegistry load_type_registry(const std::string& library_dir = "library/");

/// Merge device instance with type definition defaults
DeviceInstance merge_device_instance(
    const DeviceInstance& instance,
    const TypeDefinition& definition
);

/// Parse a TypeDefinition from JSON (helper for testing)
TypeDefinition parse_type_definition(const nlohmann::json& j);

/// Expand sub_blueprint references into flat devices + connections.
/// Throws std::runtime_error on circular references.
/// loading_stack tracks ancestors for cycle detection — pass empty set at top call.
TypeDefinition expand_sub_blueprint_references(
    const TypeDefinition& td,
    const TypeRegistry& registry,
    std::set<std::string>& loading_stack);
