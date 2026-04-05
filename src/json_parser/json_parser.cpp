#include "json_parser.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <set>
#include "../parse_number.h"

using json = nlohmann::json;

namespace json_parser_detail {
DeviceInstance parse_device_for_parser(const json& j);
Connection parse_connection_for_parser(const json& j);
PortType parse_port_type_for_parser(const std::string& s);
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

// parse_domain/parse_domain_mask_int/parse_param_schema moved to json_parser_types.cpp

static void validate_params_against_schema(
    const std::unordered_map<std::string, std::string>& params,
    const std::unordered_map<std::string, ParamSchemaEntry>& schema,
    const std::string& dev_name,
    const std::string& classname
) {
    for (const auto& [name, entry] : schema) {
        auto it = params.find(name);
        if (it == params.end()) {
            if (entry.required) {
                throw std::runtime_error("Missing required parameter '" + name + "' on device '" + dev_name + "' (" + classname + ")");
            }
            continue;
        }
        const std::string& value = it->second;
        switch (entry.type) {
            case ParamSchemaType::Float: {
                float v = 0.0f;
                if (!locale_safe::parse_float(value, v)) {
                    throw std::runtime_error("Parameter '" + name + "' must be float on device '" + dev_name + "' (" + classname + ")");
                }
                if (entry.min.has_value() && static_cast<double>(v) < *entry.min) {
                    throw std::runtime_error("Parameter '" + name + "' below min on device '" + dev_name + "' (" + classname + ")");
                }
                if (entry.max.has_value() && static_cast<double>(v) > *entry.max) {
                    throw std::runtime_error("Parameter '" + name + "' above max on device '" + dev_name + "' (" + classname + ")");
                }
                break;
            }
            case ParamSchemaType::Int: {
                long long v = 0;
                if (!locale_safe::parse_int64(value, v)) {
                    throw std::runtime_error("Parameter '" + name + "' must be int on device '" + dev_name + "' (" + classname + ")");
                }
                if (entry.min.has_value() && static_cast<double>(v) < *entry.min) {
                    throw std::runtime_error("Parameter '" + name + "' below min on device '" + dev_name + "' (" + classname + ")");
                }
                if (entry.max.has_value() && static_cast<double>(v) > *entry.max) {
                    throw std::runtime_error("Parameter '" + name + "' above max on device '" + dev_name + "' (" + classname + ")");
                }
                break;
            }
            case ParamSchemaType::Bool: {
                if (!(value == "true" || value == "false" || value == "1" || value == "0")) {
                    throw std::runtime_error("Parameter '" + name + "' must be bool on device '" + dev_name + "' (" + classname + ")");
                }
                break;
            }
            case ParamSchemaType::String:
                break;
        }
    }
}

static bool has_domain_in(const std::vector<Domain>& domains, Domain target) {
    for (Domain d : domains) {
        if (has_domain(d, target)) {
            return true;
        }
    }
    return false;
}

// parse_port_direction/parse_port_type moved to json_parser_types.cpp

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

// parse_port/parse_device/parse_connection moved to json_parser_types.cpp

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
            tpl.devices.push_back(json_parser_detail::parse_device_for_parser(dev_j));
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
    // in type definitions (library/*.blueprint).

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
                port.type = json_parser_detail::parse_port_type_for_parser(type_it->second);
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
    const bool is_top_level = expanding.empty();

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
    std::set<std::string> expanded_instance_names;
    if (j.contains("devices")) {
        for (const auto& dev_j : j["devices"]) {
            raw_devices.push_back(json_parser_detail::parse_device_for_parser(dev_j));
        }
    } else if (j.contains("top_level_devices")) {
        for (const auto& dev_j : j["top_level_devices"]) {
            raw_devices.push_back(json_parser_detail::parse_device_for_parser(dev_j));
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
            expanded_instance_names.insert(raw_dev.name);

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
            ctx.connections.push_back(json_parser_detail::parse_connection_for_parser(conn_j));
        }
    }

    // Parse optional initial values map: "device.port" -> float
    if (j.contains("initial_values")) {
        if (!j["initial_values"].is_object()) {
            throw std::runtime_error("'initial_values' must be an object");
        }
        for (const auto& [port_ref, val] : j["initial_values"].items()) {
            if (!val.is_number()) {
                throw std::runtime_error("initial_values entry '" + port_ref + "' must be numeric");
            }
            ctx.initial_values[port_ref] = val.get<float>();
        }
    }

    // Rewrite connections that point to expanded nested blueprints
    // When a blueprint "lamp_bp" is expanded, its exposed ports become "lamp_bp:vin.port"
    // So we need to rewrite "lamp_bp.vin" -> "lamp_bp:vin.port"
    // But we must NOT rewrite internal connections that already have the prefix
    // Skip this for recursive blueprint loading (only process at top level).
    if (is_top_level) {
        std::set<std::string> expanded_blueprint_names;
        expanded_blueprint_names.insert(expanded_instance_names.begin(), expanded_instance_names.end());
        // Find all expanded blueprints by looking for BlueprintInput/BlueprintOutput devices
        // These have names like "lamp_bp:vin" and "lamp_bp:vout"
        // The blueprint name is everything before the first colon
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

            // === PARITY GUARD: Parent Connection Rewrite ===
            // INVARIANT: This rewrite is CANONICAL for parent-facing composite ports.
            // - Blueprint expanded from TypeRegistry: device_name + ":" + port_name becomes exposed.
            // - Parent connections (editor/root) use format: instance:port.ext (expanded side).
            // - Internal connections (within expanded blueprint) use format: instance:port.port.
            // - Root/editor resolver must map expandable root endpoints to :instance:port.ext format.
            // - AOT and JIT solvers must agree on bridge semantics (.ext vs .port union).
            for (auto& conn : ctx.connections) {
                // Helper to rewrite one side of a connection
                auto rewrite_port = [&](std::string& port_ref) {
                    size_t dot_pos = port_ref.find('.');
                    if (dot_pos == std::string::npos) return;  // No port specified

                    std::string device_name = port_ref.substr(0, dot_pos);
                    std::string port_name = port_ref.substr(dot_pos + 1);

                    // INVARIANT: port_name must be non-empty after extraction.
                    // If port_name is empty (malformed endpoint like "instance."),
                    // skip rewrite and let downstream validation catch it.
                    if (port_name.empty()) {
                        spdlog::warn("[json_parser] Malformed endpoint (empty port): '{}' - skipping rewrite",
                                    port_ref);
                        return;
                    }

                    // Skip if already has prefix (internal connection, already processed)
                    if (device_name.find(':') != std::string::npos) {
                        return;
                    }

                    // Check if this device is an expanded blueprint
                    if (expanded_blueprint_names.count(device_name)) {
                        // Rewrite parent-facing composite ports to the bridge node's
                        // external side: "lamp_bp.vin" -> "lamp_bp:vin.ext".
                        // The internal ".port" endpoint stays for connections inside
                        // the expanded blueprint only.
                        // [PARITY] This contract must be mirrored in:
                        //   - signal_key_resolver.cpp (root/external signal mapping)
                        //   - jit_solver.cpp (BlueprintInput/BlueprintOutput bridge union)
                        //   - codegen.cpp (AOT equivalent bridge union)
                        std::string old_ref = port_ref;
                        port_ref = device_name + ":" + port_name + ".ext";
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
            spdlog::warn("[json_parser] Port '{}' already has a wire connected (one-to-one violation) - allowing duplicate",
                          conn.from);
        }
        if (!to_allows_multiple && occupied_ports.count(conn.to)) {
            spdlog::warn("[json_parser] Port '{}' already has a wire connected (one-to-one violation) - allowing duplicate",
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

// serialize_json moved to json_parser_serialize.cpp

// Helper: parse TypeDefinition from JSON
// parse_type_definition moved to json_parser_types.cpp

// load_type_registry moved to json_parser_registry.cpp

// parse_type_definition/merge_device_instance/menu+validation helpers moved to json_parser_types.cpp
