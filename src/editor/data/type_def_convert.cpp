/// Flat ↔ v1 conversion functions for library type definitions.

#include "editor/data/type_def_convert.h"
#include "../../parse_number.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <set>

// ==================================================================
// Helper: port direction string → enum and back
// ==================================================================

static PortDirection parse_direction(const std::string& dir) {
    if (dir == "In")    return PortDirection::In;
    if (dir == "Out")   return PortDirection::Out;
    if (dir == "InOut") return PortDirection::InOut;
    return PortDirection::Out;
}

static std::string direction_to_string(PortDirection dir) {
    switch (dir) {
        case PortDirection::In:    return "In";
        case PortDirection::Out:   return "Out";
        case PortDirection::InOut: return "InOut";
    }
    return "Out";
}

// ==================================================================
// Helper: port type string → enum and back
// ==================================================================

static PortType parse_port_type(const std::string& type) {
    if (type == "V")           return PortType::V;
    if (type == "I")           return PortType::I;
    if (type == "Bool")        return PortType::Bool;
    if (type == "RPM")         return PortType::RPM;
    if (type == "Temperature") return PortType::Temperature;
    if (type == "Pressure")    return PortType::Pressure;
    if (type == "Position")    return PortType::Position;
    if (type == "Any")         return PortType::Any;
    return PortType::Any;
}

static std::string port_type_to_string(PortType type) {
    switch (type) {
        case PortType::V:           return "V";
        case PortType::I:           return "I";
        case PortType::Bool:        return "Bool";
        case PortType::RPM:         return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure:    return "Pressure";
        case PortType::Position:    return "Position";
        case PortType::Any:         return "Any";
    }
    return "Any";
}

// ==================================================================
// Helper: domain string → enum and back
// ==================================================================

static Domain parse_domain(const std::string& d) {
    if (d == "Electrical") return Domain::Electrical;
    if (d == "Logical")    return Domain::Logical;
    if (d == "Mechanical") return Domain::Mechanical;
    if (d == "Hydraulic")  return Domain::Hydraulic;
    if (d == "Thermal")    return Domain::Thermal;
    return Domain::Electrical;
}

static std::string domain_to_string(Domain d) {
    if (d == Domain::Electrical) return "Electrical";
    if (d == Domain::Logical)    return "Logical";
    if (d == Domain::Mechanical) return "Mechanical";
    if (d == Domain::Hydraulic)  return "Hydraulic";
    if (d == Domain::Thermal)    return "Thermal";
    return "Electrical";
}

// ==================================================================
// type_definition_to_flat helpers
// ==================================================================

/// Infer FlatParam type from value string (locale-independent).
static std::string infer_param_type(const std::string& val) {
    if (val == "true" || val == "false") return "bool";
    if (val.find('.') != std::string::npos && locale_safe::is_float_literal(val)) return "float";
    if (locale_safe::is_int_literal(val)) return "float";  // Treat integers as float for simulation
    if (locale_safe::is_float_literal(val)) return "float";  // Scientific notation without '.'
    return "string";
}

/// Split "device.port" into ["device", "port"].
static std::pair<std::string, std::string> split_dot(const std::string& s) {
    auto dot = s.find('.');
    if (dot == std::string::npos) return {s, ""};
    return {s.substr(0, dot), s.substr(dot + 1)};
}

/// Convert TypeDefinition devices → FlatBlueprint nodes.
static void convert_devices_to_nodes(const TypeDefinition& td, FlatBlueprint& bp) {
    for (const auto& dev : td.devices) {
        FlatNode node;
        node.type = dev.classname;
        node.params = std::map<std::string, std::string>(dev.params.begin(), dev.params.end());
        if (dev.pos.has_value()) node.pos = {dev.pos->first, dev.pos->second};
        if (dev.size.has_value()) node.size = FlatPos{dev.size->first, dev.size->second};
        bp.nodes[dev.name] = node;
    }
}

/// Convert TypeDefinition connections → FlatBlueprint wires.
static void convert_connections_to_wires(const TypeDefinition& td, FlatBlueprint& bp) {
    int wire_idx = 0;
    for (const auto& conn : td.connections) {
        FlatWire wire;
        wire.id = "w" + std::to_string(wire_idx++);
        auto [from_node, from_port] = split_dot(conn.from);
        auto [to_node, to_port] = split_dot(conn.to);
        wire.from = {from_node, from_port};
        wire.to = {to_node, to_port};
        for (const auto& [x, y] : conn.routing_points) {
            wire.routing.push_back({x, y});
        }
        bp.wires.push_back(wire);
    }
}

/// Convert TypeDefinition sub_blueprints → FlatBlueprint sub_blueprints.
static void convert_sub_blueprints_to_flat(const TypeDefinition& td, FlatBlueprint& bp) {
    for (const auto& ref : td.sub_blueprints) {
        FlatSubBlueprint sb;
        sb.template_path = ref.blueprint_path;
        if (ref.pos.has_value()) sb.pos = {ref.pos->first, ref.pos->second};
        if (ref.size.has_value()) sb.size = {ref.size->first, ref.size->second};
        if (!ref.params_override.empty()) {
            FlatOverrides ov;
            ov.params = std::map<std::string, std::string>(
                ref.params_override.begin(), ref.params_override.end());
            sb.overrides = ov;
        }
        bp.sub_blueprints[ref.id] = sb;
    }
}

// ==================================================================
// type_definition_to_flat
// ==================================================================

FlatBlueprint type_definition_to_flat(const TypeDefinition& td) {
    FlatBlueprint bp;
    bp.version = 2;

    // Meta
    bp.meta.name = td.classname;
    bp.meta.description = td.description;
    bp.meta.cpp_class = td.cpp_class;
    bp.meta.priority = td.priority;
    bp.meta.critical = td.critical;
    bp.meta.content_type = td.content_type;
    bp.meta.render_hint = td.render_hint;
    bp.meta.visual_only = td.visual_only;
    if (td.size.has_value()) bp.meta.size = FlatPos{td.size->first, td.size->second};

    // Domains
    if (td.domains.has_value()) {
        for (const auto& d : *td.domains) bp.meta.domains.push_back(domain_to_string(d));
    }

    // Exposes (ports)
    for (const auto& [name, port] : td.ports) {
        FlatPort ep;
        ep.direction = direction_to_string(port.direction);
        ep.type = port_type_to_string(port.type);
        ep.alias = port.alias;
        bp.exposes[name] = ep;
    }

    // Params
    for (const auto& [key, val] : td.params) {
        bp.params[key] = FlatParam{infer_param_type(val), val};
    }

    convert_devices_to_nodes(td, bp);
    convert_connections_to_wires(td, bp);
    convert_sub_blueprints_to_flat(td, bp);

    return bp;
}

// ==================================================================
// flat_to_type_definition helpers
// ==================================================================

/// Convert FlatBlueprint nodes → TypeDefinition devices.
/// For BlueprintInput/BlueprintOutput nodes with a display_name, the device key
/// is renamed to the display_name (which matches the expose port name).
/// Returns a rename map: old_key → new_key for connection rewriting.
static std::map<std::string, std::string> convert_nodes_to_devices(const FlatBlueprint& bp, TypeDefinition& td) {
    std::map<std::string, std::string> rename;

    // Collect all node keys to detect collisions with renamed IO nodes.
    std::set<std::string> all_keys;
    for (const auto& [name, _] : bp.nodes) all_keys.insert(name);

    // Track names already claimed by renamed IO nodes to detect duplicates.
    std::set<std::string> claimed;

    for (const auto& [name, node] : bp.nodes) {
        DeviceInstance dev;
        dev.classname = node.type;
        dev.params = std::unordered_map<std::string, std::string>(
            node.params.begin(), node.params.end());
        if (node.pos[0] != 0.0f || node.pos[1] != 0.0f) dev.pos = {node.pos[0], node.pos[1]};
        if (node.size.has_value()) dev.size = {(*node.size)[0], (*node.size)[1]};

        // Canonical rename: BlueprintInput/Output nodes use their display_name
        // (the expose port name) as the device key, so that all downstream code
        // — expansion, wire rewriting, simulator signal lookup — uses a single
        // consistent name with no fallback mappings needed.
        bool is_bp_io = (node.type == "BlueprintInput" || node.type == "BlueprintOutput");
        bool renamed = false;
        if (is_bp_io && !node.display_name.empty() && node.display_name != name) {
            // Guard: skip rename if display_name collides with another node key
            // or was already claimed by a previous IO node rename.
            bool collides = (all_keys.count(node.display_name) > 0 && node.display_name != name)
                         || claimed.count(node.display_name) > 0;
            if (collides) {
                spdlog::warn("[type_def_convert] Cannot rename '{}' → '{}': name collision, keeping original key",
                             name, node.display_name);
            } else {
                dev.name = node.display_name;
                rename[name] = node.display_name;
                claimed.insert(node.display_name);
                renamed = true;
            }
        }
        if (!renamed) {
            dev.name = name;
            dev.display_name = node.display_name;
        }

        td.devices.push_back(dev);
    }
    return rename;
}

/// Convert FlatBlueprint wires → TypeDefinition connections.
/// Applies the rename map so that wires referencing old BlueprintInput/Output
/// node keys (e.g. "blueprintinput_1") are rewritten to the canonical expose
/// name (e.g. "v").
static void convert_wires_to_connections(const FlatBlueprint& bp, TypeDefinition& td,
                                         const std::map<std::string, std::string>& rename) {
    for (const auto& wire : bp.wires) {
        std::string from_node = wire.from.node;
        std::string to_node   = wire.to.node;
        auto it_from = rename.find(from_node);
        if (it_from != rename.end()) from_node = it_from->second;
        auto it_to = rename.find(to_node);
        if (it_to != rename.end()) to_node = it_to->second;

        Connection conn;
        conn.from = from_node + "." + wire.from.port;
        conn.to   = to_node   + "." + wire.to.port;
        for (const auto& pt : wire.routing) conn.routing_points.push_back({pt[0], pt[1]});
        td.connections.push_back(conn);
    }
}

/// Convert FlatBlueprint sub_blueprints → TypeDefinition sub_blueprints.
static void convert_flat_sub_blueprints(const FlatBlueprint& bp, TypeDefinition& td) {
    for (const auto& [id, sb] : bp.sub_blueprints) {
        SubBlueprintRef ref;
        ref.id = id;
        if (sb.template_path.has_value()) ref.blueprint_path = *sb.template_path;
        ref.pos = {sb.pos[0], sb.pos[1]};
        ref.size = {sb.size[0], sb.size[1]};
        if (sb.overrides.has_value()) {
            ref.params_override = std::map<std::string, std::string>(
                sb.overrides->params.begin(), sb.overrides->params.end());
        }
        td.sub_blueprints.push_back(ref);
    }
}

// ==================================================================
// flat_to_type_definition
// ==================================================================

TypeDefinition flat_to_type_definition(const FlatBlueprint& bp) {
    TypeDefinition td;

    // Meta → basic fields
    td.classname = bp.meta.name;
    td.description = bp.meta.description;
    td.cpp_class = bp.meta.cpp_class;
    td.priority = bp.meta.priority;
    td.critical = bp.meta.critical;
    td.content_type = bp.meta.content_type;
    td.render_hint = bp.meta.render_hint;
    td.visual_only = bp.meta.visual_only;
    if (bp.meta.size.has_value()) td.size = {(*bp.meta.size)[0], (*bp.meta.size)[1]};

    // Domains
    if (!bp.meta.domains.empty()) {
        std::vector<Domain> domains;
        for (const auto& d : bp.meta.domains) domains.push_back(parse_domain(d));
        td.domains = domains;
    }

    // Exposes → ports (single source of truth — no fallback scanning)
    for (const auto& [name, ep] : bp.exposes) {
        Port port;
        port.direction = parse_direction(ep.direction);
        port.type = parse_port_type(ep.type);
        port.alias = ep.alias;
        td.ports[name] = port;
    }

    // Params
    for (const auto& [key, pd] : bp.params) td.params[key] = pd.default_val;

    auto rename = convert_nodes_to_devices(bp, td);
    convert_wires_to_connections(bp, td, rename);
    convert_flat_sub_blueprints(bp, td);

    return td;
}
