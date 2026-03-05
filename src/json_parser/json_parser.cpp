#include "json_parser.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace an24 {

// Helper: convert string to Domain
static Domain parse_domain(const std::string& s) {
    if (s == "Electrical") return Domain::Electrical;
    if (s == "Hydraulic") return Domain::Hydraulic;
    if (s == "Mechanical") return Domain::Mechanical;
    if (s == "Thermal") return Domain::Thermal;
    throw std::runtime_error("Unknown domain: " + s);
}

// Helper: convert Domain to string
static std::string domain_to_string(Domain d) {
    switch (d) {
        case Domain::Electrical: return "Electrical";
        case Domain::Hydraulic: return "Hydraulic";
        case Domain::Mechanical: return "Mechanical";
        case Domain::Thermal: return "Thermal";
    }
    return "Unknown";
}

// Helper: convert string to PortDirection
static PortDirection parse_port_direction(const std::string& s) {
    if (s == "in" || s == "input" || s == "i" || s == "In") return PortDirection::In;
    if (s == "out" || s == "output" || s == "o" || s == "Out" || s == "InOut") return PortDirection::Out;
    return PortDirection::Out;  // default
}

// Helper: parse a single port
static Port parse_port(const json& j) {
    Port port;
    if (j.is_string()) {
        port.direction = parse_port_direction(j.get<std::string>());
    } else if (j.is_object()) {
        if (j.contains("direction")) {
            port.direction = parse_port_direction(j["direction"].get<std::string>());
        }
    }
    return port;
}

// Helper: parse DeviceInstance from JSON
static DeviceInstance parse_device(const json& j) {
    DeviceInstance dev;

    if (j.contains("name")) dev.name = j["name"].get<std::string>();
    if (j.contains("template")) dev.template_name = j["template"].get<std::string>();
    else if (j.contains("template_name")) dev.template_name = j["template_name"].get<std::string>();
    if (j.contains("classname")) dev.classname = j["classname"].get<std::string>();
    else throw std::runtime_error("Device missing required 'classname' field");

    if (j.contains("priority")) dev.priority = j["priority"].get<std::string>();
    if (j.contains("bucket") && !j["bucket"].is_null()) {
        dev.bucket = j["bucket"].get<size_t>();
    }
    if (j.contains("critical")) dev.critical = j["critical"].get<bool>();
    if (j.contains("is_composite")) dev.is_composite = j["is_composite"].get<bool>();

    // Ports
    if (j.contains("ports")) {
        for (auto& [port_name, port_val] : j["ports"].items()) {
            dev.ports[port_name] = parse_port(port_val);
        }
    }

    // Params (key-value pairs)
    if (j.contains("params")) {
        for (auto& [key, val] : j["params"].items()) {
            dev.params[key] = val.get<std::string>();
        }
    }

    // Domains
    if (j.contains("explicit_domains") && j["explicit_domains"].is_array()) {
        std::vector<Domain> domains;
        for (const auto& d : j["explicit_domains"]) {
            domains.push_back(parse_domain(d.get<std::string>()));
        }
        if (!domains.empty()) {
            dev.explicit_domains = domains;
        }
    } else if (j.contains("domain")) {
        std::vector<Domain> domains;
        if (j["domain"].is_string()) {
            // Single domain: "Electrical" or comma-separated: "Electrical,Hydraulic"
            std::string s = j["domain"].get<std::string>();
            size_t start = 0;
            while (start < s.size()) {
                size_t comma = s.find(',', start);
                std::string part = s.substr(start, comma - start);
                // Trim whitespace
                while (!part.empty() && (part[0] == ' ' || part[0] == '\t')) part.erase(part.begin());
                while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.pop_back();
                if (!part.empty()) {
                    domains.push_back(parse_domain(part));
                }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else if (j["domain"].is_array()) {
            for (const auto& d : j["domain"]) {
                domains.push_back(parse_domain(d.get<std::string>()));
            }
        }
        if (!domains.empty()) {
            dev.explicit_domains = domains;
        }
    }

    return dev;
}

// Helper: parse Connection from JSON
static Connection parse_connection(const json& j) {
    Connection conn;
    if (j.is_string()) {
        // Format: "from -> to" or just two-element array
        std::string s = j.get<std::string>();
        size_t arrow = s.find("->");
        if (arrow != std::string::npos) {
            conn.from = s.substr(0, arrow);
            conn.to = s.substr(arrow + 2);
            // Trim whitespace
            while (!conn.from.empty() && conn.from[0] == ' ') conn.from.erase(conn.from.begin());
            while (!conn.to.empty() && conn.to[0] == ' ') conn.to.erase(conn.to.begin());
            while (!conn.from.empty() && conn.from.back() == ' ') conn.from.pop_back();
            while (!conn.to.empty() && conn.to.back() == ' ') conn.to.pop_back();
        }
    } else if (j.is_object()) {
        if (j.contains("from")) conn.from = j["from"].get<std::string>();
        if (j.contains("to")) conn.to = j["to"].get<std::string>();
    }
    return conn;
}

// Helper: parse SubsystemCall from JSON
static SubsystemCall parse_subsystem(const json& j) {
    SubsystemCall sub;
    if (j.contains("name")) sub.name = j["name"].get<std::string>();
    if (j.contains("template")) sub.template_name = j["template"].get<std::string>();
    if (j.contains("port_map")) {
        for (auto& [external, internal] : j["port_map"].items()) {
            sub.port_map[external] = internal.get<std::string>();
        }
    }
    return sub;
}

// Helper: parse SystemTemplate from JSON
static SystemTemplate parse_template(const json& j) {
    SystemTemplate tpl;
    if (j.contains("name")) tpl.name = j["name"].get<std::string>();

    if (j.contains("devices")) {
        for (const auto& dev_j : j["devices"]) {
            tpl.devices.push_back(parse_device(dev_j));
        }
    }

    if (j.contains("subsystems")) {
        for (const auto& sub_j : j["subsystems"]) {
            tpl.subsystems.push_back(parse_subsystem(sub_j));
        }
    }

    if (j.contains("exposed_ports")) {
        for (auto& [external, internal] : j["exposed_ports"].items()) {
            tpl.exposed_ports[external] = internal.get<std::string>();
        }
    }

    if (j.contains("domain")) {
        // Parse domain similar to DeviceInstance
        std::vector<Domain> domains;
        json::const_reference dom = j["domain"];
        if (dom.is_string()) {
            std::string s = dom.get<std::string>();
            size_t start = 0;
            while (start < s.size()) {
                size_t comma = s.find(',', start);
                std::string part = s.substr(start, comma - start);
                while (!part.empty() && (part[0] == ' ' || part[0] == '\t')) part.erase(part.begin());
                while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.pop_back();
                if (!part.empty()) domains.push_back(parse_domain(part));
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else if (dom.is_array()) {
            for (const auto& d : dom) {
                domains.push_back(parse_domain(d.get<std::string>()));
            }
        }
        if (!domains.empty()) tpl.explicit_domains = domains;
    }

    return tpl;
}

ParserContext parse_json(const std::string& json_text) {
    spdlog::debug("[json_parser] Parsing JSON text");

    auto j = json::parse(json_text);
    ParserContext ctx;

    // Load component registry
    ctx.registry = load_component_registry();
    spdlog::info("[json_parser] Loaded {} component definitions", ctx.registry.components.size());

    // Parse templates
    if (j.contains("templates")) {
        for (auto& [name, tpl_j] : j["templates"].items()) {
            auto tpl = parse_template(tpl_j);
            if (tpl.name.empty()) tpl.name = name;
            ctx.templates[tpl.name] = tpl;
        }
    }

    // Parse devices (also accepts "top_level_devices")
    std::vector<DeviceInstance> raw_devices;
    if (j.contains("devices")) {
        for (const auto& dev_j : j["devices"]) {
            raw_devices.push_back(parse_device(dev_j));
        }
    } else if (j.contains("top_level_devices")) {
        for (const auto& dev_j : j["top_level_devices"]) {
            raw_devices.push_back(parse_device(dev_j));
        }
    }

    // Merge with component definitions and validate
    for (const auto& raw_dev : raw_devices) {
        // Check if component exists in registry
        if (!ctx.registry.has(raw_dev.classname)) {
            spdlog::error("[json_parser] Unknown component classname '{}' in device '{}'",
                         raw_dev.classname, raw_dev.name);
            throw std::runtime_error("Unknown component classname: " + raw_dev.classname);
        }

        // Get component definition
        const auto* def = ctx.registry.get(raw_dev.classname);
        if (!def) {
            spdlog::error("[json_parser] Component definition not found for '{}' in device '{}'",
                         raw_dev.classname, raw_dev.name);
            throw std::runtime_error("Component definition not found: " + raw_dev.classname);
        }

        // Merge instance with definition
        DeviceInstance merged = merge_device_instance(raw_dev, *def);

        // Validate merged instance
        auto error = ctx.registry.validate_instance(merged);
        if (error.has_value()) {
            spdlog::error("[json_parser] Validation failed for device '{}': {}",
                         merged.name, error.value());
            throw std::runtime_error("Device validation failed: " + error.value());
        }

        ctx.devices.push_back(merged);
        spdlog::debug("[json_parser] Merged device '{}' of type '{}' with component definition",
                     merged.name, merged.classname);
    }

    // Parse connections
    if (j.contains("connections")) {
        for (const auto& conn_j : j["connections"]) {
            ctx.connections.push_back(parse_connection(conn_j));
        }
    }

    spdlog::debug("[json_parser] Parsed {} templates, {} devices, {} connections",
        ctx.templates.size(), ctx.devices.size(), ctx.connections.size());

    return ctx;
}

// Serialization helpers
static json port_to_json(const Port& port) {
    json j;
    j["direction"] = (port.direction == PortDirection::In) ? "in" : "out";
    return j;
}

static json device_to_json(const DeviceInstance& dev) {
    json j;
    if (!dev.name.empty()) j["name"] = dev.name;
    if (!dev.template_name.empty()) j["template"] = dev.template_name;
    if (!dev.classname.empty()) j["classname"] = dev.classname;
    if (dev.priority != "med") j["priority"] = dev.priority;
    if (dev.bucket.has_value()) j["bucket"] = dev.bucket.value();
    if (dev.critical) j["critical"] = true;
    if (dev.is_composite) j["is_composite"] = true;

    if (!dev.ports.empty()) {
        json ports;
        for (const auto& [name, port] : dev.ports) {
            ports[name] = port_to_json(port);
        }
        j["ports"] = ports;
    }

    if (!dev.params.empty()) {
        json params;
        for (const auto& [key, val] : dev.params) {
            params[key] = val;
        }
        j["params"] = params;
    }

    if (dev.explicit_domains.has_value()) {
        std::string domains_str;
        for (size_t i = 0; i < dev.explicit_domains->size(); ++i) {
            if (i > 0) domains_str += ",";
            domains_str += domain_to_string((*dev.explicit_domains)[i]);
        }
        j["domain"] = domains_str;
    }

    return j;
}

static json connection_to_json(const Connection& conn) {
    json j;
    j["from"] = conn.from;
    j["to"] = conn.to;
    return j;
}

static json subsystem_to_json(const SubsystemCall& sub) {
    json j;
    if (!sub.name.empty()) j["name"] = sub.name;
    j["template"] = sub.template_name;
    if (!sub.port_map.empty()) {
        json port_map;
        for (const auto& [ext, int_] : sub.port_map) {
            port_map[ext] = int_;
        }
        j["port_map"] = port_map;
    }
    return j;
}

static json template_to_json(const SystemTemplate& tpl) {
    json j;
    if (!tpl.name.empty()) j["name"] = tpl.name;

    if (!tpl.devices.empty()) {
        json devices;
        for (const auto& dev : tpl.devices) {
            devices.push_back(device_to_json(dev));
        }
        j["devices"] = devices;
    }

    if (!tpl.subsystems.empty()) {
        json subsystems;
        for (const auto& sub : tpl.subsystems) {
            subsystems.push_back(subsystem_to_json(sub));
        }
        j["subsystems"] = subsystems;
    }

    if (!tpl.exposed_ports.empty()) {
        json exposed;
        for (const auto& [ext, int_] : tpl.exposed_ports) {
            exposed[ext] = int_;
        }
        j["exposed_ports"] = exposed;
    }

    if (tpl.explicit_domains.has_value()) {
        std::string domains_str;
        for (size_t i = 0; i < tpl.explicit_domains->size(); ++i) {
            if (i > 0) domains_str += ",";
            domains_str += domain_to_string((*tpl.explicit_domains)[i]);
        }
        j["domain"] = domains_str;
    }

    return j;
}

std::string serialize_json(const ParserContext& ctx) {
    json j;

    // Templates
    if (!ctx.templates.empty()) {
        json templates;
        for (const auto& [name, tpl] : ctx.templates) {
            templates[name] = template_to_json(tpl);
        }
        j["templates"] = templates;
    }

    // Devices
    if (!ctx.devices.empty()) {
        json devices;
        for (const auto& dev : ctx.devices) {
            devices.push_back(device_to_json(dev));
        }
        j["devices"] = devices;
    }

    // Connections
    if (!ctx.connections.empty()) {
        json connections;
        for (const auto& conn : ctx.connections) {
            connections.push_back(connection_to_json(conn));
        }
        j["connections"] = connections;
    }

    return j.dump(2);  // Pretty print with 2-space indent
}

// Helper: parse ComponentDefinition from JSON
static ComponentDefinition parse_component_definition(const json& j) {
    ComponentDefinition def;

    if (j.contains("classname")) def.classname = j["classname"].get<std::string>();
    else throw std::runtime_error("Component definition missing 'classname' field");

    if (j.contains("description")) def.description = j["description"].get<std::string>();

    // Parse default ports
    if (j.contains("default_ports")) {
        for (auto& [port_name, port_val] : j["default_ports"].items()) {
            def.default_ports[port_name] = parse_port(port_val);
        }
    }

    // Parse default params
    if (j.contains("default_params")) {
        for (auto& [key, val] : j["default_params"].items()) {
            def.default_params[key] = val.get<std::string>();
        }
    }

    // Parse default domains
    if (j.contains("default_domains") && j["default_domains"].is_array()) {
        std::vector<Domain> domains;
        for (const auto& d : j["default_domains"]) {
            domains.push_back(parse_domain(d.get<std::string>()));
        }
        if (!domains.empty()) {
            def.default_domains = domains;
        }
    }

    // Parse default priority
    if (j.contains("default_priority")) {
        def.default_priority = j["default_priority"].get<std::string>();
    }

    // Parse default critical
    if (j.contains("default_critical")) {
        def.default_critical = j["default_critical"].get<bool>();
    }

    return def;
}

// Load component registry from components/ directory
ComponentRegistry load_component_registry(const std::string& components_dir) {
    ComponentRegistry registry;

    std::filesystem::path components_path(components_dir);

    // If relative path doesn't exist, try relative to source directory
    if (!std::filesystem::exists(components_path) && components_path.is_relative()) {
        // Try common locations relative to current working directory
        std::vector<std::filesystem::path> try_paths = {
            components_path,  // As provided
            "../" / components_path,  // Parent directory
            "../../" / components_path,  // Two levels up
            "../../../" / components_path,  // Three levels up (for build/tests)
        };

        for (const auto& path : try_paths) {
            if (std::filesystem::exists(path)) {
                components_path = path;
                break;
            }
        }
    }

    // Check if directory exists
    if (!std::filesystem::exists(components_path)) {
        spdlog::warn("[json_parser] Components directory '{}' does not exist, using empty registry", components_dir);
        return registry;
    }

    // Scan for *.json files
    size_t loaded_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(components_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            try {
                // Read and parse JSON file
                std::ifstream file(entry.path());
                json j;
                file >> j;

                // Parse component definition
                ComponentDefinition def = parse_component_definition(j);

                // Check for duplicate classnames
                if (registry.has(def.classname)) {
                    spdlog::error("[json_parser] Duplicate classname '{}' in '{}', skipping",
                                  def.classname, entry.path().string());
                    continue;
                }

                // Add to registry
                registry.components[def.classname] = def;
                loaded_count++;

                spdlog::debug("[json_parser] Loaded component definition: '{}' from {}",
                             def.classname, entry.path().filename().string());
            }
            catch (const std::exception& e) {
                spdlog::error("[json_parser] Failed to parse component definition '{}': {}",
                             entry.path().string(), e.what());
            }
        }
    }

    spdlog::info("[json_parser] Loaded {} component definitions from '{}'",
                 loaded_count, components_dir);

    return registry;
}

// Merge device instance with component definition defaults
DeviceInstance merge_device_instance(
    const DeviceInstance& instance,
    const ComponentDefinition& definition
) {
    DeviceInstance merged = instance;

    // Merge ports: instance overrides, defaults fill gaps
    if (merged.ports.empty()) {
        merged.ports = definition.default_ports;
    } else {
        for (const auto& [port_name, port] : definition.default_ports) {
            if (!merged.ports.count(port_name)) {
                merged.ports[port_name] = port;
            }
        }
    }

    // Merge params: instance overrides, defaults fill gaps
    for (const auto& [param_name, param_value] : definition.default_params) {
        if (!merged.params.count(param_name)) {
            merged.params[param_name] = param_value;
        }
    }

    // Merge domains: instance overrides default
    if (!merged.explicit_domains.has_value() && definition.default_domains.has_value()) {
        merged.explicit_domains = definition.default_domains;
    }

    // Merge priority: instance overrides default (only if instance has default priority)
    if (merged.priority == "med" && definition.default_priority != "med") {
        merged.priority = definition.default_priority;
    }

    // Merge critical: instance overrides default
    if (!merged.critical && definition.default_critical) {
        merged.critical = true;
    }

    return merged;
}

// Validate instance against component definition
std::optional<std::string> ComponentRegistry::validate_instance(const DeviceInstance& instance) const {
    // Check classname exists
    if (!has(instance.classname)) {
        return "Unknown classname '" + instance.classname + "' in device '" + instance.name + "'";
    }

    const auto* def = get(instance.classname);
    if (!def) {
        return "Component definition not found for '" + instance.classname + "'";
    }

    // Validate instance ports are known in definition
    for (const auto& [port_name, port] : instance.ports) {
        if (!def->default_ports.count(port_name)) {
            return "Unknown port '" + port_name + "' in device '" + instance.name +
                   "' of type '" + instance.classname + "'. Valid ports: " +
                   [&]() {
                       std::string valid_ports;
                       for (const auto& [name, _] : def->default_ports) {
                           if (!valid_ports.empty()) valid_ports += ", ";
                           valid_ports += name;
                       }
                       return valid_ports;
                   }();
        }
    }

    // Validate domains are specified
    if (!instance.explicit_domains.has_value() && !def->default_domains.has_value()) {
        return "No domains specified for device '" + instance.name + "' of type '" + instance.classname + "'";
    }

    return std::nullopt;  // Validation passed
}

} // namespace an24
