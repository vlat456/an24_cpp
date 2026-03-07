#include "json_parser.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <set>

using json = nlohmann::json;

namespace an24 {

// Helper: convert string to Domain
static Domain parse_domain(const std::string& s) {
    if (s == "Electrical") return Domain::Electrical;
    if (s == "Logical") return Domain::Logical;
    if (s == "Hydraulic") return Domain::Hydraulic;
    if (s == "Mechanical") return Domain::Mechanical;
    if (s == "Thermal") return Domain::Thermal;
    throw std::runtime_error("Unknown domain: " + s);
}

// Helper: convert Domain to string
static std::string domain_to_string(Domain d) {
    switch (d) {
        case Domain::Electrical: return "Electrical";
        case Domain::Logical: return "Logical";
        case Domain::Hydraulic: return "Hydraulic";
        case Domain::Mechanical: return "Mechanical";
        case Domain::Thermal: return "Thermal";
    }
    return "Unknown";
}

// Helper: convert string to PortDirection
static PortDirection parse_port_direction(const std::string& s) {
    if (s == "in" || s == "input" || s == "i" || s == "In") return PortDirection::In;
    if (s == "InOut" || s == "inout" || s == "io") return PortDirection::InOut;  // [g7h8]
    if (s == "out" || s == "output" || s == "o" || s == "Out") return PortDirection::Out;
    return PortDirection::Out;  // default
}

// Helper: convert string to PortType
static PortType parse_port_type(const std::string& s) {
    if (s == "V") return PortType::V;
    if (s == "I") return PortType::I;
    if (s == "Bool") return PortType::Bool;
    if (s == "RPM") return PortType::RPM;
    if (s == "Temperature") return PortType::Temperature;
    if (s == "Pressure") return PortType::Pressure;
    if (s == "Position") return PortType::Position;
    if (s == "Any") return PortType::Any;
    throw std::runtime_error("Unknown port type: " + s);
}

// Helper: convert PortType to string
static std::string port_type_to_string(PortType t) {
    switch (t) {
        case PortType::V: return "V";
        case PortType::I: return "I";
        case PortType::Bool: return "Bool";
        case PortType::RPM: return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure: return "Pressure";
        case PortType::Position: return "Position";
        case PortType::Any: return "Any";
    }
    return "Unknown";
}

// Check if two port types are compatible for connection
static bool are_ports_compatible(PortType from_type, PortType to_type) {
    // Any type is wildcard - compatible with everything
    if (from_type == PortType::Any || to_type == PortType::Any) {
        return true;
    }
    // Types must match exactly
    return from_type == to_type;
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
        if (j.contains("type")) {
            port.type = parse_port_type(j["type"].get<std::string>());
        }
        if (j.contains("alias")) {
            port.alias = j["alias"].get<std::string>();
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

    // NOTE: Domains are NOT parsed from JSON - they are defined exclusively
    // in component definitions (components/*.json). Users cannot override domains.

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

    // NOTE: Domains are NOT parsed from JSON - they are defined exclusively
    // in component definitions (components/*.json).

    return tpl;
}

/// Merge nested blueprint into parent context with name prefixing
/// Phase 2.3: Helper for recursive blueprint loading
static void merge_nested_blueprint(
    ParserContext& parent,
    const ParserContext& nested,
    const std::string& prefix  // e.g., "battery_module"
) {
    spdlog::debug("[parser] Merging nested blueprint '{}' with {} devices, {} connections",
                  prefix, nested.devices.size(), nested.connections.size());

    // Prefix all nested device names: "bat" -> "battery_module:bat"
    for (const auto& dev : nested.devices) {
        DeviceInstance prefixed = dev;
        prefixed.name = prefix + ":" + dev.name;
        parent.devices.push_back(prefixed);
    }

    // Rewrite connections with prefix: "vin.port" -> "battery_module:vin.port"
    for (const auto& conn : nested.connections) {
        Connection rewritten = conn;
        rewritten.from = prefix + ":" + conn.from;
        rewritten.to = prefix + ":" + conn.to;
        parent.connections.push_back(rewritten);
    }
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
            // Phase 2: Fallback to blueprint loading
            std::string blueprint_path = "blueprints/" + raw_dev.classname + ".json";
            if (std::filesystem::exists(blueprint_path)) {
                spdlog::info("[json_parser] Loading nested blueprint '{}' from {}",
                           raw_dev.classname, blueprint_path);

                // Load nested blueprint file
                std::ifstream blueprint_file(blueprint_path);
                if (!blueprint_file.is_open()) {
                    throw std::runtime_error("Failed to open blueprint: " + blueprint_path);
                }

                std::string blueprint_json((std::istreambuf_iterator<char>(blueprint_file)),
                                           std::istreambuf_iterator<char>());

                // Recursively parse nested blueprint (DRY - same code path!)
                ParserContext nested = parse_json(blueprint_json);

                // Merge nested blueprint with prefix
                merge_nested_blueprint(ctx, nested, raw_dev.name);

                spdlog::info("[json_parser] Merged nested blueprint '{}' as device '{}'",
                           raw_dev.classname, raw_dev.name);
                continue;  // Skip normal device processing
            }

            // Not in registry and no blueprint found - error!
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

    // Validate one-to-one connections (except for Bus/RefNode)
    // Log warnings but don't reject - allow existing blueprints to load
    // Track which ports are already connected
    std::set<std::string> occupied_ports;
    for (const auto& conn : ctx.connections) {
        // Parse connection strings
        size_t from_dot = conn.from.find('.');
        size_t to_dot = conn.to.find('.');
        if (from_dot == std::string::npos || to_dot == std::string::npos) {
            throw std::runtime_error("Invalid connection format: " + conn.from + " -> " + conn.to);
        }

        std::string from_device = conn.from.substr(0, from_dot);
        std::string from_port = conn.from.substr(from_dot + 1);
        std::string to_device = conn.to.substr(0, to_dot);
        std::string to_port = conn.to.substr(to_dot + 1);

        // Check if devices allow multiple connections (Bus, RefNode)
        auto* from_dev = ctx.find_device(from_device);
        auto* to_dev = ctx.find_device(to_device);

        bool from_allows_multiple = (from_dev &&
            (from_dev->classname == "Bus" || from_dev->classname == "RefNode"));
        bool to_allows_multiple = (to_dev &&
            (to_dev->classname == "Bus" || to_dev->classname == "RefNode"));

        // Check if ports are already occupied - log warning but don't throw
        if (!from_allows_multiple && occupied_ports.count(conn.from)) {
            spdlog::warn("[json_parser] Port '{}' already has a wire connected (one-to-one violation) - allowing for compatibility",
                          conn.from);
        }
        if (!to_allows_multiple && occupied_ports.count(conn.to)) {
            spdlog::warn("[json_parser] Port '{}' already has a wire connected (one-to-one violation) - allowing for compatibility",
                          conn.to);
        }

        // Mark ports as occupied
        if (!from_allows_multiple) {
            occupied_ports.insert(conn.from);
        }
        if (!to_allows_multiple) {
            occupied_ports.insert(conn.to);
        }
    }

    // NOTE: Port type validation is done during wire creation in the editor,
    // not during JSON parsing. This allows loading existing blueprints and
    // gives better error messages when users try to create incompatible connections.

    spdlog::debug("[json_parser] Parsed {} templates, {} devices, {} connections",
        ctx.templates.size(), ctx.devices.size(), ctx.connections.size());

    return ctx;
}

// Serialization helpers
static json port_to_json(const Port& port) {
    json j;
    // [g7h8] serialize all three directions
    switch (port.direction) {
        case PortDirection::In:    j["direction"] = "In"; break;
        case PortDirection::InOut: j["direction"] = "InOut"; break;
        default:                   j["direction"] = "Out"; break;
    }
    j["type"] = port_type_to_string(port.type);
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

    // NOTE: Domains are NOT serialized to JSON - they are defined exclusively
    // in component definitions (components/*.json).

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

    // NOTE: Domains are NOT serialized to JSON

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

    // Parse default content type
    if (j.contains("default_content_type")) {
        def.default_content_type = j["default_content_type"].get<std::string>();
    }

    // Parse default size {x, y} in grid units
    if (j.contains("default_size") && j["default_size"].is_object()) {
        auto size_obj = j["default_size"];
        if (size_obj.contains("x") && size_obj.contains("y")) {
            float x = size_obj["x"].get<float>();
            float y = size_obj["y"].get<float>();
            def.default_size = {x, y};
        }
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
            } else {
                // Port exists in instance - always copy type and alias from definition
                // This ensures port types from component definitions are always used
                merged.ports[port_name].type = port.type;
                merged.ports[port_name].alias = port.alias;
            }
        }
    }

    // Merge params: instance overrides, defaults fill gaps
    for (const auto& [param_name, param_value] : definition.default_params) {
        if (!merged.params.count(param_name)) {
            merged.params[param_name] = param_value;
        }
    }

    // Copy domains from component definition (NOT user-configurable)
    if (definition.default_domains.has_value()) {
        merged.domains = *definition.default_domains;
    } else {
        merged.domains = {Domain::Electrical};  // Default fallback
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
    if (instance.domains.empty()) {
        return "No domains specified for device '" + instance.name + "' of type '" + instance.classname + "'";
    }

    return std::nullopt;  // Validation passed
}

} // namespace an24
