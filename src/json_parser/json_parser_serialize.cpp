#include "json_parser.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

std::string port_type_to_string(PortType t) {
    switch (t) {
        case PortType::V: return "V";
        case PortType::I: return "I";
        case PortType::Signal: return "Signal";
        case PortType::Bool: return "Bool";
        case PortType::RPM: return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure: return "Pressure";
        case PortType::Position: return "Position";
        case PortType::Contextual: return "Contextual";
        case PortType::Any: return "Any";
    }
    return "Unknown";
}

json port_to_json(const Port& port) {
    json j;
    switch (port.direction) {
        case bp2::Direction::Input:  j["direction"] = "In"; break;
        case bp2::Direction::InOut: j["direction"] = "InOut"; break;
        default:                     j["direction"] = "Out"; break;
    }
    j["type"] = port_type_to_string(port.type);
    return j;
}

json device_to_json(const DeviceInstance& dev) {
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

    return j;
}

json device_to_json(const ResolvedDevice& dev) {
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

    return j;
}

json connection_to_json(const Connection& conn) {
    json j;
    j["from"] = conn.from;
    j["to"] = conn.to;
    return j;
}

json subsystem_to_json(const SubsystemCall& sub) {
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

json template_to_json(const SystemTemplate& tpl) {
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

    return j;
}

} // namespace

std::string serialize_json(const ParserContext& ctx) {
    json j;

    if (!ctx.templates.empty()) {
        json templates;
        for (const auto& [name, tpl] : ctx.templates) {
            templates[name] = template_to_json(tpl);
        }
        j["templates"] = templates;
    }

    if (!ctx.devices.empty()) {
        json devices;
        for (const auto& dev : ctx.devices) {
            devices.push_back(device_to_json(dev));
        }
        j["devices"] = devices;
    }

    if (!ctx.connections.empty()) {
        json connections;
        for (const auto& conn : ctx.connections) {
            connections.push_back(connection_to_json(conn));
        }
        j["connections"] = connections;
    }

    if (!ctx.initial_values.empty()) {
        json initial_values;
        for (const auto& [port_ref, value] : ctx.initial_values) {
            initial_values[port_ref] = value;
        }
        j["initial_values"] = initial_values;
    }

    return j.dump(2);
}
