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
        } else {
            throw std::runtime_error("Port definition missing required 'type' field");
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
    // in type definitions (library/*.json). Users cannot override domains.

    // Editor layout (optional, for blueprint expansion)
    if (j.contains("pos") && j["pos"].is_object()) {
        auto& p = j["pos"];
        if (p.contains("x") && p.contains("y"))
            dev.pos = {p["x"].get<float>(), p["y"].get<float>()};
    }
    if (j.contains("size") && j["size"].is_object()) {
        auto& s = j["size"];
        if (s.contains("x") && s.contains("y"))
            dev.size = {s["x"].get<float>(), s["y"].get<float>()};
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

    // NOTE: Domains are NOT parsed from JSON - they are defined exclusively
    // in type definitions (library/*.json).

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

/// Extract exposed port metadata from BlueprintInput/BlueprintOutput devices
/// For Editor: displays exposed ports on collapsed nested blueprint nodes
/// Returns map: exposed_port_name -> Port metadata
std::unordered_map<std::string, Port> extract_exposed_ports(
    const ParserContext& blueprint
) {
    std::unordered_map<std::string, Port> exposed;

    for (const auto& dev : blueprint.devices) {
        if (dev.classname == "BlueprintInput" || dev.classname == "BlueprintOutput") {
            // The device NAME is the exposed port name (e.g., "vin", "vout")
            std::string exposed_name = dev.name;

            // Get metadata from params
            Port port;
            auto dir_it = dev.params.find("exposed_direction");
            if (dir_it != dev.params.end()) {
                port.direction = (dir_it->second == "In") ? PortDirection::In : PortDirection::Out;
            } else {
                port.direction = (dev.classname == "BlueprintInput") ? PortDirection::Out : PortDirection::In;
            }

            auto type_it = dev.params.find("exposed_type");
            if (type_it != dev.params.end()) {
                port.type = parse_port_type(type_it->second);
            } else {
                port.type = PortType::Any;  // Default
            }

            port.alias = std::nullopt;  // Exposed ports don't have aliases
            exposed[exposed_name] = port;

            spdlog::debug("[parser] Exposed port: {} ({}, {})",
                         exposed_name,
                         (port.direction == PortDirection::In) ? "In" : "Out",
                         port_type_to_string(port.type));
        }
    }

    return exposed;
}

static ParserContext parse_json_impl(const std::string& json_text,
                                     TypeRegistry& registry,
                                     std::set<std::string> expanding) {
    spdlog::debug("[json_parser] Parsing JSON text");

    auto j = json::parse(json_text);
    ParserContext ctx;

    // Share registry (already loaded by caller)
    ctx.registry = registry;

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

        // Blueprint types (cpp_class=false): expand from TypeDefinition
        if (!def->cpp_class && !def->devices.empty()) {
            // Cycle detection: if we're already expanding this classname, it's a cycle
            if (expanding.count(raw_dev.classname)) {
                throw std::runtime_error("Blueprint cycle detected: '" + raw_dev.classname +
                    "' is already being expanded (circular dependency)");
            }

            spdlog::info("[json_parser] Expanding blueprint type '{}' as device '{}' from TypeRegistry",
                        raw_dev.classname, raw_dev.name);

            // Build a ParserContext from the TypeDefinition's devices/connections
            // and recursively process them (handles nested blueprints)
            nlohmann::json nested_json;
            nested_json["devices"] = nlohmann::json::array();
            for (const auto& inner_dev : def->devices) {
                nlohmann::json dev_j;
                dev_j["name"] = inner_dev.name;
                dev_j["classname"] = inner_dev.classname;
                if (!inner_dev.params.empty()) {
                    dev_j["params"] = inner_dev.params;
                }
                nested_json["devices"].push_back(dev_j);
            }
            nested_json["connections"] = nlohmann::json::array();
            for (const auto& conn : def->connections) {
                nested_json["connections"].push_back({{"from", conn.from}, {"to", conn.to}});
            }

            // Track this classname as being expanded, then recurse
            expanding.insert(raw_dev.classname);
            ParserContext nested = parse_json_impl(nested_json.dump(), registry, expanding);
            merge_nested_blueprint(ctx, nested, raw_dev.name);

            spdlog::info("[json_parser] Expanded blueprint '{}' as device '{}' ({} devices)",
                        raw_dev.classname, raw_dev.name, nested.devices.size());
            continue;
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

    // Rewrite connections that point to expanded nested blueprints
    // When a blueprint "lamp_bp" is expanded, its exposed ports become "lamp_bp:vin.port"
    // So we need to rewrite "lamp_bp.vin" -> "lamp_bp:vin.port"
    // But we must NOT rewrite internal connections that already have the prefix
    // Skip this for recursive blueprint loading (only process at top level)
    if (ctx.devices.empty() || ctx.devices[0].name.find(':') == std::string::npos || true) {  // TODO: better detection
        std::set<std::string> expanded_blueprint_names;
        // Find all expanded blueprints by looking for BlueprintInput/BlueprintOutput devices
        // These have names like "lamp_bp:vin" and "lamp_bp:vout"
        // The blueprint name is everything before the LAST colon
        for (const auto& dev : ctx.devices) {
            if (dev.classname == "BlueprintInput" || dev.classname == "BlueprintOutput") {
                // Extract blueprint name (everything before the colon)
                size_t colon_pos = dev.name.find(':');
                if (colon_pos != std::string::npos) {
                    std::string blueprint_name = dev.name.substr(0, colon_pos);
                    expanded_blueprint_names.insert(blueprint_name);
                    spdlog::debug("[json_parser] Found expanded blueprint from {}: blueprint '{}'",
                                dev.classname, blueprint_name);
                }
            }
        }

        if (!expanded_blueprint_names.empty()) {
            spdlog::info("[json_parser] Found {} expanded blueprints: [{}]",
                         expanded_blueprint_names.size(),
                         fmt::join(expanded_blueprint_names, ", "));

            // Rewrite connections (only parent connections, not internal ones)
            for (auto& conn : ctx.connections) {
                // Helper to rewrite one side of a connection
                auto rewrite_port = [&](std::string& port_ref) {
                    size_t dot_pos = port_ref.find('.');
                    if (dot_pos == std::string::npos) return;  // No port specified

                    std::string device_name = port_ref.substr(0, dot_pos);
                    std::string port_name = port_ref.substr(dot_pos + 1);

                    // Skip if already has prefix (internal connection, already processed)
                    if (device_name.find(':') != std::string::npos) {
                        return;
                    }

                    // Check if this device is an expanded blueprint
                    if (expanded_blueprint_names.count(device_name)) {
                        // Rewrite: "lamp_bp.vin" -> "lamp_bp:vin.port"
                        std::string old_ref = port_ref;
                        port_ref = device_name + ":" + port_name + ".port";
                        spdlog::info("[json_parser] Rewrote parent connection: '{}' -> '{}'",
                                    old_ref, port_ref);
                    }
                };

                rewrite_port(conn.from);
                rewrite_port(conn.to);
            }
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

ParserContext parse_json(const std::string& json_text) {
    auto registry = load_type_registry();
    spdlog::info("[json_parser] Loaded {} type definitions", registry.types.size());
    return parse_json_impl(json_text, registry, {});
}

ParserContext parse_json(const std::string& json_text, const std::string& library_dir) {
    auto registry = load_type_registry(library_dir);
    spdlog::info("[json_parser] Loaded {} type definitions from '{}'", registry.types.size(), library_dir);
    return parse_json_impl(json_text, registry, {});
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
    // in type definitions (library/*.json).

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

// Helper: parse TypeDefinition from JSON
static TypeDefinition parse_type_definition(const json& j) {
    TypeDefinition def;

    if (j.contains("classname")) def.classname = j["classname"].get<std::string>();
    else throw std::runtime_error("Type definition missing 'classname' field");

    if (j.contains("description")) def.description = j["description"].get<std::string>();
    if (j.contains("cpp_class")) def.cpp_class = j["cpp_class"].get<bool>();

    // Parse ports
    if (j.contains("ports")) {
        for (auto& [port_name, port_val] : j["ports"].items()) {
            def.ports[port_name] = parse_port(port_val);
        }
    }

    // Parse params
    if (j.contains("params")) {
        for (auto& [key, val] : j["params"].items()) {
            def.params[key] = val.get<std::string>();
        }
    }

    // Parse domains
    if (j.contains("domains") && j["domains"].is_array()) {
        std::vector<Domain> domains;
        for (const auto& d : j["domains"]) {
            domains.push_back(parse_domain(d.get<std::string>()));
        }
        if (!domains.empty()) {
            def.domains = domains;
        }
    }

    // Parse priority
    if (j.contains("priority")) {
        def.priority = j["priority"].get<std::string>();
    }

    // Parse critical
    if (j.contains("critical")) {
        def.critical = j["critical"].get<bool>();
    }

    // Parse content type
    if (j.contains("content_type")) {
        def.content_type = j["content_type"].get<std::string>();
    }

    // Parse render hint (visual style: "bus", "ref", or empty)
    if (j.contains("render_hint")) {
        def.render_hint = j["render_hint"].get<std::string>();
    }

    // Parse size {x, y} in grid units
    if (j.contains("size") && j["size"].is_object()) {
        auto size_obj = j["size"];
        if (size_obj.contains("x") && size_obj.contains("y")) {
            float x = size_obj["x"].get<float>();
            float y = size_obj["y"].get<float>();
            def.size = {x, y};
        }
    }

    // For blueprints: parse devices and connections
    if (j.contains("devices") && j["devices"].is_array()) {
        for (const auto& dev_j : j["devices"]) {
            def.devices.push_back(parse_device(dev_j));
        }
    }
    if (j.contains("connections") && j["connections"].is_array()) {
        for (const auto& conn_j : j["connections"]) {
            def.connections.push_back(parse_connection(conn_j));
        }
    }
    // Also accept "wires" (editor format) as connections, with routing_points
    if (def.connections.empty() && j.contains("wires") && j["wires"].is_array()) {
        for (const auto& wire_j : j["wires"]) {
            Connection conn = parse_connection(wire_j);
            if (wire_j.contains("routing_points") && wire_j["routing_points"].is_array()) {
                for (const auto& rp : wire_j["routing_points"]) {
                    if (rp.contains("x") && rp.contains("y")) {
                        conn.routing_points.push_back({rp["x"].get<float>(), rp["y"].get<float>()});
                    }
                }
            }
            def.connections.push_back(std::move(conn));
        }
    }

    return def;
}

// Load type registry from library/ directory
TypeRegistry load_type_registry(const std::string& library_dir) {
    TypeRegistry registry;

    std::filesystem::path library_path(library_dir);

    // If relative path doesn't exist, try relative to source directory
    if (!std::filesystem::exists(library_path) && library_path.is_relative()) {
        // Try common locations relative to current working directory
        std::vector<std::filesystem::path> try_paths = {
            library_path,  // As provided
            "../" / library_path,  // Parent directory
            "../../" / library_path,  // Two levels up
            "../../../" / library_path,  // Three levels up (for build/tests)
        };

        for (const auto& path : try_paths) {
            if (std::filesystem::exists(path)) {
                library_path = path;
                break;
            }
        }
    }

    // Check if directory exists
    if (!std::filesystem::exists(library_path)) {
        spdlog::warn("[json_parser] Library directory '{}' does not exist, using empty registry", library_dir);
        return registry;
    }

    // Scan for *.json files recursively
    size_t loaded_count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(library_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            try {
                // Read and parse JSON file
                std::ifstream file(entry.path());
                json j;
                file >> j;

                // Parse type definition
                TypeDefinition def = parse_type_definition(j);

                // Check for duplicate classnames
                if (registry.has(def.classname)) {
                    spdlog::error("[json_parser] Duplicate classname '{}' in '{}', skipping",
                                  def.classname, entry.path().string());
                    continue;
                }

                // Compute category from relative directory path
                auto relative_dir = std::filesystem::relative(entry.path().parent_path(), library_path);
                std::string category = relative_dir.generic_string();
                if (category == ".") category = "";

                // Add to registry
                registry.types[def.classname] = def;
                if (!category.empty()) {
                    registry.categories[def.classname] = category;
                }
                loaded_count++;

                spdlog::debug("[json_parser] Loaded type definition: '{}' from {} (category: {})",
                             def.classname, entry.path().filename().string(),
                             category.empty() ? "root" : category);
            }
            catch (const std::exception& e) {
                spdlog::error("[json_parser] Failed to parse type definition '{}': {}",
                             entry.path().string(), e.what());
            }
        }
    }

    spdlog::info("[json_parser] Loaded {} type definitions from '{}'",
                 loaded_count, library_dir);

    return registry;
}

// Merge device instance with component definition defaults
DeviceInstance merge_device_instance(
    const DeviceInstance& instance,
    const TypeDefinition& definition
) {
    DeviceInstance merged = instance;

    // Merge ports: instance overrides, defaults fill gaps
    if (merged.ports.empty()) {
        merged.ports = definition.ports;
    } else {
        for (const auto& [port_name, port] : definition.ports) {
            if (!merged.ports.count(port_name)) {
                merged.ports[port_name] = port;
            } else {
                // Port exists in instance - always copy type and alias from definition
                // This ensures port types from type definitions are always used
                merged.ports[port_name].type = port.type;
                merged.ports[port_name].alias = port.alias;
            }
        }
    }

    // Merge params: instance overrides, defaults fill gaps
    for (const auto& [param_name, param_value] : definition.params) {
        if (!merged.params.count(param_name)) {
            merged.params[param_name] = param_value;
        }
    }

    // Copy domains from type definition (NOT user-configurable)
    if (definition.domains.has_value()) {
        merged.domains = *definition.domains;
    } else {
        merged.domains = {Domain::Electrical};  // Default fallback
    }

    // Merge priority: instance overrides default (only if instance has default priority)
    if (merged.priority == "med" && definition.priority != "med") {
        merged.priority = definition.priority;
    }

    // Merge critical: instance overrides default
    if (!merged.critical && definition.critical) {
        merged.critical = true;
    }

    return merged;
}

// Build menu tree from directory hierarchy (categories map)
MenuTree TypeRegistry::build_menu_tree() const {
    MenuTree root;
    for (const auto& [classname, _] : types) {
        MenuTree* node = &root;

        auto cat_it = categories.find(classname);
        if (cat_it != categories.end() && !cat_it->second.empty()) {
            const std::string& cat = cat_it->second;
            size_t start = 0;
            while (start < cat.size()) {
                size_t slash = cat.find('/', start);
                std::string segment = (slash == std::string::npos)
                    ? cat.substr(start)
                    : cat.substr(start, slash - start);
                node = &node->children[segment];
                start = (slash == std::string::npos) ? cat.size() : slash + 1;
            }
        }

        node->entries.push_back(classname);
    }

    // Sort entries at every level
    std::function<void(MenuTree&)> sort_tree = [&](MenuTree& t) {
        std::sort(t.entries.begin(), t.entries.end());
        for (auto& [_, child] : t.children) {
            sort_tree(child);
        }
    };
    sort_tree(root);

    return root;
}

// Validate instance against type definition
std::optional<std::string> TypeRegistry::validate_instance(const DeviceInstance& instance) const {
    // Check classname exists
    if (!has(instance.classname)) {
        return "Unknown classname '" + instance.classname + "' in device '" + instance.name + "'";
    }

    const auto* def = get(instance.classname);
    if (!def) {
        return "Type definition not found for '" + instance.classname + "'";
    }

    // Validate instance ports are known in definition
    for (const auto& [port_name, port] : instance.ports) {
        if (!def->ports.count(port_name)) {
            return "Unknown port '" + port_name + "' in device '" + instance.name +
                   "' of type '" + instance.classname + "'. Valid ports: " +
                   [&]() {
                       std::string valid_ports;
                       for (const auto& [name, _] : def->ports) {
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
