#include "json_parser.h"
#include "json_parser_internal_utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <set>
#include "../parse_number.h"
#include "core/solvers/common/signal_key.h"

using json = nlohmann::json;

namespace json_parser_detail {
DeviceInstance parse_device_for_parser(const json& j);
Connection parse_connection_for_parser(const json& j);
PortType parse_port_type_for_parser(const std::string& s);
}

// parse_domain/parse_domain_mask_int/parse_param_schema moved to shared json_parser_internal_utils

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
        prefixed.name = signal_key::make_child_scope_key(prefix, dev.name);
        parent.devices.push_back(prefixed);
    }

    // Rewrite connections with prefix: "vin.port" -> "battery_module:vin.port"
    for (const auto& conn : nested.connections) {
        Connection rewritten = conn;
        rewritten.from = signal_key::make_child_scope_key(prefix, conn.from);
        rewritten.to = signal_key::make_child_scope_key(prefix, conn.to);
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

    // NOTE: One-to-one connection validation is not enforced at JSON parse time.
    // The canonical runtime/editor path uses pairwise validation during blueprint elaboration.

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
