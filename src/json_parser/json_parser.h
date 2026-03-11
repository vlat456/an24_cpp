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

namespace an24 {

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
    Bool,         // Boolean (logic level, on/off)
    RPM,          // Rotational speed (revolutions per minute)
    Temperature,  // Temperature (degrees Celsius)
    Pressure,     // Pressure (Pascal, bar, etc.)
    Position,     // Position/Displacement (mechanical position)
    Any,          // Wildcard - can connect to any type (for adapters)
};

/// Port direction
enum class PortDirection {
    In,
    Out,
    InOut  // [g7h8] bidirectional port
};

/// Single port definition
struct Port {
    PortDirection direction = PortDirection::Out;
    PortType type = PortType::Any;  // Port type for validation
    std::optional<std::string> alias;  // If set, this port is an alias to another port (e.g., "out1" -> "in")
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

/// Type definition (ports, params, domains for a component class or blueprint)
struct TypeDefinition {
    std::string classname;                    // C++ class name or blueprint classname (e.g., "Battery", "SimpleBattery")
    std::string description;                  // Human-readable description
    bool cpp_class = true;                    // true = C++ component, false = blueprint
    std::unordered_map<std::string, Port> ports;  // Port definitions
    std::unordered_map<std::string, std::string> params;  // Default parameter values
    std::optional<std::vector<Domain>> domains;    // Domains
    std::string priority = "med";     // Priority
    bool critical = false;            // Critical flag
    std::string content_type = "None"; // UI content type (None, Gauge, Switch, Text)
    std::string render_hint;  // Visual hint for editor rendering ("bus", "ref", or empty)
    bool visual_only = false;  // True = no simulation behavior (e.g. Group)
    std::optional<std::pair<float, float>> size;  // Size in grid units {width, height}
    // For blueprints only: internal devices and connections
    std::vector<DeviceInstance> devices;  // Internal devices (for blueprints)
    std::vector<Connection> connections;  // Internal connections (for blueprints)
    // Sub-blueprint references (cpp_class = false composites only)
    std::vector<SubBlueprintRef> sub_blueprints;
};

/// Tree structure mirroring library/ subdirectory hierarchy for menu building.
struct MenuTree {
    std::vector<std::string> entries;                        // Classnames at this level (sorted)
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
    std::string priority = "med";  // high, med, low
    std::optional<size_t> bucket;  // computation bucket
    bool critical = false;
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, std::string> params;
    std::vector<Domain> domains;  // From component definition only, NOT user-configurable
    bool visual_only = false;      // True = no simulation behavior (e.g. Group)
    std::optional<std::pair<float,float>> pos;   // Editor layout position (optional)
    std::optional<std::pair<float,float>> size;  // Editor layout size (optional)

    // Default constructor
    DeviceInstance() = default;

    // Convenience constructor for testing/debug (PortDirection version)
    DeviceInstance(
        const std::string& name_,
        const std::string& classname_,
        std::unordered_map<std::string, std::string> params_ = {},
        std::unordered_map<std::string, PortDirection> ports_ = {}
    ) : name(name_), classname(classname_), params(std::move(params_)) {
        // Convert PortDirection to Port
        for (const auto& [port_name, direction] : ports_) {
            PortType type = PortType::Any;
            if (port_name.find('v') != std::string::npos) type = PortType::V;
            else if (port_name.find('i') != std::string::npos) type = PortType::I;
            else if (port_name.find("rpm") != std::string::npos) type = PortType::RPM;
            ports[port_name] = Port{direction, type, std::nullopt};
        }
    }

    // Convenience constructor for testing/debug (string port direction version)
    DeviceInstance(
        const std::string& name_,
        const std::string& classname_,
        std::unordered_map<std::string, std::string> params_,
        std::unordered_map<std::string, std::string> ports_
    ) : name(name_), classname(classname_), params(std::move(params_)) {
        // Convert string port direction to Port
        for (const auto& [port_name, dir_str] : ports_) {
            PortDirection dir = (dir_str == "in" || dir_str == "i" || dir_str == "input") ? PortDirection::In : PortDirection::Out;
            PortType type = PortType::Any;
            if (port_name.find('v') != std::string::npos) type = PortType::V;
            else if (port_name.find('i') != std::string::npos) type = PortType::I;
            else if (port_name.find("rpm") != std::string::npos) type = PortType::RPM;
            ports[port_name] = Port{dir, type, std::nullopt};
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

/// Extract exposed port metadata from BlueprintInput/BlueprintOutput devices
/// For Editor: displays exposed ports on collapsed nested blueprint nodes
/// Returns map: exposed_port_name -> Port metadata
std::unordered_map<std::string, Port> extract_exposed_ports(const ParserContext& blueprint);

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

} // namespace an24
