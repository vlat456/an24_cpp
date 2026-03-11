/// v2 ↔ v1 conversion functions for library type definitions.

#include "convert.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace an24::v2 {

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
// type_definition_to_v2
// ==================================================================

BlueprintV2 type_definition_to_v2(const TypeDefinition& td) {
    BlueprintV2 bp;
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

    if (td.size.has_value()) {
        bp.meta.size = Pos{td.size->first, td.size->second};
    }

    // Domains
    if (td.domains.has_value()) {
        for (const auto& d : *td.domains) {
            bp.meta.domains.push_back(domain_to_string(d));
        }
    }

    // Exposes (ports)
    for (const auto& [name, port] : td.ports) {
        ExposedPort ep;
        ep.direction = direction_to_string(port.direction);
        ep.type = port_type_to_string(port.type);
        ep.alias = port.alias;
        bp.exposes[name] = ep;
    }

    // Params
    if (td.cpp_class) {
        // C++ component: params as {type, default} — infer type from value
        for (const auto& [key, val] : td.params) {
            ParamDef pd;
            pd.default_val = val;
            // Infer type from value string
            if (val == "true" || val == "false") {
                pd.type = "bool";
            } else if (val.find('.') != std::string::npos) {
                // Check if it's a valid float
                try {
                    std::stof(val);
                    pd.type = "float";
                } catch (...) {
                    pd.type = "string";
                }
            } else {
                // Try integer
                try {
                    std::stoi(val);
                    pd.type = "float";  // Treat integers as float for simulation
                } catch (...) {
                    pd.type = "string";
                }
            }
            bp.params[key] = pd;
        }
    }

    // Nodes (from devices, for composites only)
    for (const auto& dev : td.devices) {
        NodeV2 node;
        node.type = dev.classname;
        node.params = std::map<std::string, std::string>(dev.params.begin(), dev.params.end());
        if (dev.pos.has_value()) {
            node.pos = {dev.pos->first, dev.pos->second};
        }
        if (dev.size.has_value()) {
            node.size = {dev.size->first, dev.size->second};
        }
        bp.nodes[dev.name] = node;
    }

    // Wires (from connections, for composites only)
    int wire_idx = 0;
    for (const auto& conn : td.connections) {
        WireV2 wire;
        wire.id = "w" + std::to_string(wire_idx++);

        // Split "device.port" into ["device", "port"]
        auto split = [](const std::string& s) -> std::pair<std::string, std::string> {
            auto dot = s.find('.');
            if (dot == std::string::npos) return {s, ""};
            return {s.substr(0, dot), s.substr(dot + 1)};
        };

        auto [from_node, from_port] = split(conn.from);
        auto [to_node, to_port] = split(conn.to);

        wire.from = {from_node, from_port};
        wire.to = {to_node, to_port};

        // Routing points
        for (const auto& [x, y] : conn.routing_points) {
            wire.routing.push_back({x, y});
        }

        bp.wires.push_back(wire);
    }

    // Sub-blueprints
    for (const auto& ref : td.sub_blueprints) {
        SubBlueprintV2 sb;
        sb.template_path = ref.blueprint_path;
        if (ref.pos.has_value()) {
            sb.pos = {ref.pos->first, ref.pos->second};
        }
        if (ref.size.has_value()) {
            sb.size = {ref.size->first, ref.size->second};
        }
        if (!ref.params_override.empty()) {
            OverridesV2 ov;
            ov.params = std::map<std::string, std::string>(
                ref.params_override.begin(), ref.params_override.end());
            sb.overrides = ov;
        }
        bp.sub_blueprints[ref.id] = sb;
    }

    return bp;
}

// ==================================================================
// v2_to_type_definition
// ==================================================================

TypeDefinition v2_to_type_definition(const BlueprintV2& bp) {
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

    if (bp.meta.size.has_value()) {
        td.size = {(*bp.meta.size)[0], (*bp.meta.size)[1]};
    }

    // Domains
    if (!bp.meta.domains.empty()) {
        std::vector<Domain> domains;
        for (const auto& d : bp.meta.domains) {
            domains.push_back(parse_domain(d));
        }
        td.domains = domains;
    }

    // Exposes → ports
    for (const auto& [name, ep] : bp.exposes) {
        Port port;
        port.direction = parse_direction(ep.direction);
        port.type = parse_port_type(ep.type);
        port.alias = ep.alias;
        td.ports[name] = port;
    }

    // Params
    if (bp.meta.cpp_class) {
        // C++ component: extract default_val from ParamDef
        for (const auto& [key, pd] : bp.params) {
            td.params[key] = pd.default_val;
        }
    }

    // Nodes → devices (composites only)
    for (const auto& [name, node] : bp.nodes) {
        DeviceInstance dev;
        dev.name = name;
        dev.classname = node.type;
        dev.params = std::unordered_map<std::string, std::string>(
            node.params.begin(), node.params.end());
        if (node.pos[0] != 0.0f || node.pos[1] != 0.0f) {
            dev.pos = {node.pos[0], node.pos[1]};
        }
        if (node.size[0] != 0.0f || node.size[1] != 0.0f) {
            dev.size = {node.size[0], node.size[1]};
        }
        td.devices.push_back(dev);
    }

    // Wires → connections
    for (const auto& wire : bp.wires) {
        Connection conn;
        conn.from = wire.from.node + "." + wire.from.port;
        conn.to = wire.to.node + "." + wire.to.port;
        for (const auto& pt : wire.routing) {
            conn.routing_points.push_back({pt[0], pt[1]});
        }
        td.connections.push_back(conn);
    }

    // Sub-blueprints
    for (const auto& [id, sb] : bp.sub_blueprints) {
        SubBlueprintRef ref;
        ref.id = id;
        if (sb.template_path.has_value()) {
            ref.blueprint_path = *sb.template_path;
        }
        ref.pos = {sb.pos[0], sb.pos[1]};
        ref.size = {sb.size[0], sb.size[1]};
        if (sb.overrides.has_value()) {
            ref.params_override = std::map<std::string, std::string>(
                sb.overrides->params.begin(), sb.overrides->params.end());
        }
        td.sub_blueprints.push_back(ref);
    }

    return td;
}

} // namespace an24::v2
