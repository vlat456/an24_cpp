#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>

namespace an24 {

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
    Out
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

/// Device instance at any level (primitive or composite)
struct DeviceInstance {
    std::string name;
    std::string template_name;  // template used to instantiate this device
    std::string internal;        // internal type (Battery, Relay, etc.)
    std::string priority = "med";  // high, med, low
    std::optional<size_t> bucket;  // computation bucket
    bool critical = false;
    bool is_composite = false;
    std::unordered_map<std::string, Port> ports;
    std::unordered_map<std::string, std::string> params;
    std::optional<std::vector<Domain>> explicit_domains;

    /// Get domains for this device (throws if not specified)
    std::vector<Domain> get_domains() const {
        if (explicit_domains.has_value()) {
            return explicit_domains.value();
        }
        throw std::runtime_error(
            "Device '" + name + "' (" + internal + ") has no explicit domain declaration. "
            "Add domain='Electrical' or similar.");
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
    std::optional<std::vector<Domain>> explicit_domains;
};

/// Compilation context - holds all parsed data
struct ParserContext {
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

} // namespace an24
