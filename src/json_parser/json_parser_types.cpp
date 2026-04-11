#include "json_parser.h"
#include "json_parser_internal_utils.h"

#include "../parse_number.h"

#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

PortDirection parse_port_direction(const std::string& s) {
    if (s == "in" || s == "input" || s == "i" || s == "In") return PortDirection::In;
    if (s == "InOut" || s == "inout" || s == "io") return PortDirection::InOut;
    if (s == "out" || s == "output" || s == "o" || s == "Out") return PortDirection::Out;
    return PortDirection::Out;
}

PortType parse_port_type(const std::string& s) {
    return json_parser_internal::parse_port_type_string(s);
}

Port parse_port(const json& j) {
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
        if (j.contains("domain")) {
            if (!j["domain"].is_number_integer()) {
                throw std::runtime_error("Port definition field 'domain' must be integer bitmask");
            }
            port.domain = json_parser_internal::parse_domain_mask_int(j["domain"].get<int>());
        } else {
            // Derive domain from port type when not explicitly provided
            port.domain = domain_for_port_type(port.type);
        }
        if (j.contains("source_writer")) {
            if (!j["source_writer"].is_boolean()) {
                throw std::runtime_error("Port definition field 'source_writer' must be bool");
            }
            port.source_writer = j["source_writer"].get<bool>();
        }
        if (j.contains("alias")) {
            port.alias = j["alias"].get<std::string>();
        }
    }
    return port;
}

Connection parse_connection(const json& j) {
    Connection conn;
    if (j.is_string()) {
        std::string s = j.get<std::string>();
        size_t arrow = s.find("->");
        if (arrow != std::string::npos) {
            conn.from = s.substr(0, arrow);
            conn.to = s.substr(arrow + 2);
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

DeviceInstance parse_device(const json& j) {
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

    if (j.contains("ports")) {
        for (auto& [port_name, port_val] : j["ports"].items()) {
            dev.ports[port_name] = parse_port(port_val);
        }
    }

    if (j.contains("params")) {
        for (auto& [key, val] : j["params"].items()) {
            dev.params[key] = val.get<std::string>();
        }
    }

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

void validate_params_against_schema(
    const std::unordered_map<std::string, std::string>& params,
    const std::unordered_map<std::string, ParamSchemaEntry>& schema,
    const std::string& dev_name,
    const std::string& classname)
{
    json_parser_internal::validate_params_against_schema(params, schema, dev_name, classname);
}

} // namespace

namespace json_parser_detail {

DeviceInstance parse_device_for_parser(const json& j) {
    return parse_device(j);
}

Connection parse_connection_for_parser(const json& j) {
    return parse_connection(j);
}

PortType parse_port_type_for_parser(const std::string& s) {
    return json_parser_internal::parse_port_type_string(s);
}

} // namespace json_parser_detail

TypeDefinition parse_type_definition(const json& j) {
    TypeDefinition def;

    if (j.contains("classname")) def.classname = j["classname"].get<std::string>();
    else throw std::runtime_error("Type definition missing 'classname' field");

    if (j.contains("description")) def.description = j["description"].get<std::string>();
    if (j.contains("cpp_class")) def.cpp_class = j["cpp_class"].get<bool>();

    if (j.contains("ports")) {
        for (auto& [port_name, port_val] : j["ports"].items()) {
            def.ports[port_name] = parse_port(port_val);
        }
    }

    if (j.contains("params")) {
        for (auto& [key, val] : j["params"].items()) {
            if (val.is_string()) {
                def.params[key] = val.get<std::string>();
            } else if (val.is_object() && val.contains("default")) {
                def.params[key] = val["default"].get<std::string>();
            }
        }
    }
    if (j.contains("param_schema")) {
        def.param_schema = json_parser_internal::parse_param_schema(j["param_schema"]);
    }

    if (!j.contains("domains") || !j["domains"].is_array()) {
        throw std::runtime_error("Type definition missing required 'domains' array for component '" + def.classname + "'");
    }
    std::vector<Domain> domains;
    for (const auto& d : j["domains"]) {
        domains.push_back(json_parser_internal::parse_domain_string(d.get<std::string>()));
    }
    if (domains.empty()) {
        throw std::runtime_error("Type definition has empty 'domains' array for component '" + def.classname + "'");
    }
    def.domains = std::move(domains);

    if (j.contains("priority")) {
        def.priority = j["priority"].get<std::string>();
    }

    if (j.contains("critical")) {
        def.critical = j["critical"].get<bool>();
    }

    if (j.contains("content_type")) {
        def.content_type = j["content_type"].get<std::string>();
    }

    if (j.contains("render_hint")) {
        def.render_hint = j["render_hint"].get<std::string>();
    }

    if (j.contains("visual_only")) {
        def.visual_only = j["visual_only"].get<bool>();
    }

    if (j.contains("scheduler_source")) {
        def.scheduler_source = j["scheduler_source"].get<bool>();
    }

    if (j.contains("size") && j["size"].is_object()) {
        auto size_obj = j["size"];
        if (size_obj.contains("x") && size_obj.contains("y")) {
            float x = size_obj["x"].get<float>();
            float y = size_obj["y"].get<float>();
            def.size = {x, y};
        }
    }

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

    if (j.contains("sub_blueprints") && j["sub_blueprints"].is_array()) {
        for (const auto& sbj : j["sub_blueprints"]) {
            SubBlueprintRef ref;
            ref.id = sbj.value("id", "");
            ref.blueprint_path = sbj.value("blueprint_path", "");
            ref.type_name = sbj.value("type_name", "");
            if (sbj.contains("pos"))
                ref.pos = {sbj["pos"].value("x", 0.0f), sbj["pos"].value("y", 0.0f)};
            if (sbj.contains("size"))
                ref.size = {sbj["size"].value("x", 0.0f), sbj["size"].value("y", 0.0f)};
            if (sbj.contains("params_override") && sbj["params_override"].is_object()) {
                for (auto& [k, v] : sbj["params_override"].items())
                    ref.params_override[k] = v.get<std::string>();
            }
            def.sub_blueprints.push_back(std::move(ref));
        }
    }

    return def;
}

DeviceInstance merge_device_instance(
    const DeviceInstance& instance,
    const TypeDefinition& definition)
{
    DeviceInstance merged = instance;

    if (merged.ports.empty()) {
        merged.ports = definition.ports;
    } else {
        for (const auto& [port_name, port] : definition.ports) {
            if (!merged.ports.count(port_name)) {
                merged.ports[port_name] = port;
            } else {
                merged.ports[port_name].type = port.type;
                merged.ports[port_name].alias = port.alias;
                merged.ports[port_name].domain = port.domain;
                merged.ports[port_name].source_writer = port.source_writer;
            }
        }
    }

    for (const auto& [param_name, param_value] : definition.params) {
        auto schema_it = definition.param_schema.find(param_name);
        if (schema_it != definition.param_schema.end() && schema_it->second.visual_only) {
            continue;
        }
        if (!merged.params.count(param_name)) {
            merged.params[param_name] = param_value;
        }
    }

    for (auto it = merged.params.begin(); it != merged.params.end(); ) {
        auto schema_it = definition.param_schema.find(it->first);
        if (schema_it != definition.param_schema.end() && schema_it->second.visual_only) {
            it = merged.params.erase(it);
        } else {
            ++it;
        }
    }

    if (!definition.domains.has_value() || definition.domains->empty()) {
        throw std::runtime_error(
            "Missing domains metadata in type definition for component '" + definition.classname + "'");
    }
    merged.domains = *definition.domains;

    if (merged.priority == "med" && definition.priority != "med") {
        merged.priority = definition.priority;
    }

    if (!merged.critical && definition.critical) {
        merged.critical = true;
    }

    if (definition.visual_only) {
        merged.visual_only = true;
    }

    merged.execution = definition.execution;
    merged.scheduler_source = definition.scheduler_source;
    merged.solver_owned_electrical = definition.solver_owned_electrical;
    merged.solver_role = definition.solver_role;

    if (!definition.param_schema.empty()) {
        json_parser_internal::validate_params_against_schema(merged.params, definition.param_schema, merged.name, merged.classname);
    }

    return merged;
}

// menu tree, validation, and composite expansion moved to json_parser_model.cpp
