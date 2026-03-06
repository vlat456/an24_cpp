#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>

namespace an24 {

// Forward declarations
struct DeviceInstance;

/// Domain types for multi-domain simulation
enum class Domain {
    Electrical,  // 60 Hz - fast electrical dynamics
    Hydraulic,   // 5 Hz - slow fluid dynamics
    Mechanical,  // 20 Hz - medium mechanical systems
    Thermal      // 1 Hz - very slow temperature changes
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
};

/// Connection between two ports: "device.port" -> "device.port"
struct Connection {
    std::string from;  // "device.port"
    std::string to;    // "device.port"
};

/// Component definition (default ports, params, domains for a component class)
struct ComponentDefinition {
    std::string classname;                    // C++ class name (e.g., "Battery")
    std::string description;                  // Human-readable description
    std::unordered_map<std::string, Port> default_ports;  // Default port definitions
    std::unordered_map<std::string, std::string> default_params;  // Default parameter values
    std::optional<std::vector<Domain>> default_domains;    // Default domains
    std::string default_priority = "med";     // Default priority
    bool default_critical = false;            // Default critical flag
    std::string default_content_type = "None"; // Default UI content type (None, Gauge, Switch, Text)
};

/// Component registry - holds all component definitions loaded from components/
struct ComponentRegistry {
    std::unordered_map<std::string, ComponentDefinition> components;

    /// Get component definition by classname
    const ComponentDefinition* get(const std::string& classname) const {
        auto it = components.find(classname);
        if (it != components.end()) {
            return &it->second;
        }
        return nullptr;
    }

    /// Check if classname exists
    bool has(const std::string& classname) const {
        return components.count(classname) > 0;
    }

    /// Get all registered classnames
    std::vector<std::string> list_classnames() const {
        std::vector<std::string> names;
        names.reserve(components.size());
        for (const auto& [name, _] : components) {
            names.push_back(name);
        }
        return names;
    }

    /// Validate instance against definition
    std::optional<std::string> validate_instance(const DeviceInstance& instance) const;
};

/// Device instance at any level (primitive or composite)
struct DeviceInstance {
    std::string name;
    std::string template_name;  // template used to instantiate this device
    std::string classname;      // component class name (e.g., "Battery")
    std::string priority = "med";  // high, med, low
    std::optional<size_t> bucket;  // computation bucket
    bool critical = false;
    bool is_composite = false;
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, std::string> params;
    std::vector<Domain> domains;  // From component definition only, NOT user-configurable

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
            ports[port_name] = Port{direction};
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
            ports[port_name] = Port{dir};
        }
    }

    /// Get domains for this device
    std::vector<Domain> get_domains() const {
        if (domains.empty()) {
            throw std::runtime_error(
                "Device '" + name + "' (" + classname + ") has no domains. "
                "Component definition should have default_domains.");
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
    ComponentRegistry registry;               // Component registry
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

/// Serialize a ParserContext to pretty-printed JSON
std::string serialize_json(const ParserContext& ctx);

/// Load component registry from components/ directory
ComponentRegistry load_component_registry(const std::string& components_dir = "components/");

/// Merge device instance with component definition defaults
DeviceInstance merge_device_instance(
    const DeviceInstance& instance,
    const ComponentDefinition& definition
);

} // namespace an24
