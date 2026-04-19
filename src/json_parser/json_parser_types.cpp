#include "json_parser.h"
#include "json_parser_internal_utils.h"

#include "../parse_number.h"

#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

bp2::Direction parse_port_direction(const std::string& s) {
    if (s == "in" || s == "input" || s == "i" || s == "In") return bp2::Direction::Input;
    if (s == "InOut" || s == "inout" || s == "io") return bp2::Direction::InOut;
    if (s == "out" || s == "output" || s == "o" || s == "Out") return bp2::Direction::Output;
    return bp2::Direction::Output;
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
    const std::unordered_map<std::string, ParamSpec>& schema,
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

std::pair<ComponentSpec, TypePresentation> parse_type_definition(const json& j) {
    ComponentSpec spec;
    TypePresentation pres;

    std::string classname;
    if (j.contains("classname")) classname = j["classname"].get<std::string>();
    else throw std::runtime_error("Type definition missing 'classname' field");

    if (j.contains("description")) pres.description = j["description"].get<std::string>();

    // Determine if primitive (cpp_class=true) or composite (cpp_class=false)
    bool is_cpp_class = j.value("cpp_class", false);

    if (j.contains("priority")) {
        if (is_cpp_class) {
            spec = PrimitiveSpec{};
            std::get<PrimitiveSpec>(spec).priority = j["priority"].get<std::string>();
        } else {
            spec = CompositeSpec{};
            std::get<CompositeSpec>(spec).priority = j["priority"].get<std::string>();
        }
    } else if (is_cpp_class) {
        spec = PrimitiveSpec{};
    } else {
        spec = CompositeSpec{};
    }

    // Set classname on the spec
    if (is_cpp_class) {
        std::get<PrimitiveSpec>(spec).classname = classname;
    } else {
        std::get<CompositeSpec>(spec).classname = classname;
    }

    if (j.contains("ports")) {
        for (auto& [port_name, port_val] : j["ports"].items()) {
            Port port = parse_port(port_val);
            if (is_cpp_class) {
                std::get<PrimitiveSpec>(spec).ports[port_name] = port;
            } else {
                std::get<CompositeSpec>(spec).ports[port_name] = port;
            }
        }
    }

    json_parser_internal::merge_params_and_schema(j, "params", is_cpp_class
        ? std::get<PrimitiveSpec>(spec).params
        : std::get<CompositeSpec>(spec).params);

    if (!j.contains("domains") || !j["domains"].is_array()) {
        throw std::runtime_error("Type definition missing required 'domains' array for component '" + classname + "'");
    }
    std::vector<Domain> domains;
    for (const auto& d : j["domains"]) {
        domains.push_back(json_parser_internal::parse_domain_string(d.get<std::string>()));
    }
    if (domains.empty()) {
        throw std::runtime_error("Type definition has empty 'domains' array for component '" + classname + "'");
    }
    if (is_cpp_class) {
        std::get<PrimitiveSpec>(spec).domains = std::move(domains);
    } else {
        std::get<CompositeSpec>(spec).domains = std::move(domains);
    }

    if (j.contains("critical")) {
        bool critical = j["critical"].get<bool>();
        if (is_cpp_class) {
            std::get<PrimitiveSpec>(spec).critical = critical;
        } else {
            std::get<CompositeSpec>(spec).critical = critical;
        }
    }

    if (j.contains("content_type")) {
        pres.content_type = j["content_type"].get<std::string>();
    }

    if (j.contains("render_hint")) {
        pres.render_hint = j["render_hint"].get<std::string>();
    }

    if (j.contains("visual_only")) {
        bool visual_only = j["visual_only"].get<bool>();
        if (is_cpp_class) {
            std::get<PrimitiveSpec>(spec).visual_only = visual_only;
        } else {
            std::get<CompositeSpec>(spec).visual_only = visual_only;
        }
    }

    if (j.contains("scheduler_source")) {
        bool scheduler_source = j["scheduler_source"].get<bool>();
        if (is_cpp_class) {
            std::get<PrimitiveSpec>(spec).scheduler_source = scheduler_source;
        } else {
            std::get<CompositeSpec>(spec).scheduler_source = scheduler_source;
        }
    }

    if (j.contains("solver_owned_electrical")) {
        bool solver_owned = j["solver_owned_electrical"].get<bool>();
        if (is_cpp_class) {
            std::get<PrimitiveSpec>(spec).solver_owned_electrical = solver_owned;
        } else {
            std::get<CompositeSpec>(spec).solver_owned_electrical = solver_owned;
        }
    }

    if (j.contains("execution") && j["execution"].is_object() && is_cpp_class) {
        ExecutionPhases exec;
        auto& e = j["execution"];
        exec.electrical_passive = e.value("electrical_passive", false);
        exec.electrical_observer = e.value("electrical_observer", false);
        exec.logical = e.value("logical", false);
        exec.control_commit = e.value("control_commit", false);
        exec.electrical_actuator = e.value("electrical_actuator", false);
        exec.finalize = e.value("finalize", false);
        exec.mechanical = e.value("mechanical", false);
        exec.hydraulic = e.value("hydraulic", false);
        exec.thermal = e.value("thermal", false);
        std::get<PrimitiveSpec>(spec).execution = exec;
    }

    if (j.contains("solver_role") && j["solver_role"].is_object() && is_cpp_class) {
        SolverRole role;
        const auto& sr = j["solver_role"];
        role.kind = sr.value("kind", "");
        if (sr.contains("port_map") && sr["port_map"].is_object()) {
            for (auto& [k, v] : sr["port_map"].items()) {
                role.port_map[k] = v.get<std::string>();
            }
        }
        if (sr.contains("param_map") && sr["param_map"].is_object()) {
            for (auto& [k, v] : sr["param_map"].items()) {
                role.param_map[k] = v.get<std::string>();
            }
        }
        if (sr.contains("value_map") && sr["value_map"].is_object()) {
            for (auto& [k, v] : sr["value_map"].items()) {
                role.value_map[k] = v.get<float>();
            }
        }
        std::get<PrimitiveSpec>(spec).solver_role = role;
    }

    if (j.contains("size") && j["size"].is_object()) {
        auto size_obj = j["size"];
        if (size_obj.contains("x") && size_obj.contains("y")) {
            float x = size_obj["x"].get<float>();
            float y = size_obj["y"].get<float>();
            pres.default_size = {x, y};
        }
    }

    // Composite-only fields
    if (!is_cpp_class) {
        auto& composite = std::get<CompositeSpec>(spec);
        
        if (j.contains("devices") && j["devices"].is_array()) {
            for (const auto& dev_j : j["devices"]) {
                composite.devices.push_back(parse_device(dev_j));
            }
        }
        if (j.contains("connections") && j["connections"].is_array()) {
            for (const auto& conn_j : j["connections"]) {
                composite.connections.push_back(parse_connection(conn_j));
            }
        }
        if (composite.connections.empty() && j.contains("wires") && j["wires"].is_array()) {
            for (const auto& wire_j : j["wires"]) {
                Connection conn = parse_connection(wire_j);
                if (wire_j.contains("routing_points") && wire_j["routing_points"].is_array()) {
                    for (const auto& rp : wire_j["routing_points"]) {
                        if (rp.contains("x") && rp.contains("y")) {
                            conn.routing_points.push_back({rp["x"].get<float>(), rp["y"].get<float>()});
                        }
                    }
                }
                composite.connections.push_back(std::move(conn));
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
                composite.sub_blueprints.push_back(std::move(ref));
            }
        }
    }

    return {spec, pres};
}

DeviceInstance merge_device_instance(
    const DeviceInstance& instance,
    const ComponentSpec& definition)
{
    DeviceInstance merged = instance;

    // Get ports based on spec type
    const auto& ports = spec_ports(definition);
    
    if (merged.ports.empty()) {
        merged.ports = ports;
    } else {
        for (const auto& [port_name, port] : ports) {
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

    const auto& params = spec_params(definition);
    for (const auto& [param_name, param_spec] : params) {
        if (param_spec.visual_only) {
            continue;
        }
        if (!merged.params.count(param_name)) {
            merged.params[param_name] = param_spec.default_value;
        }
    }

    for (auto it = merged.params.begin(); it != merged.params.end(); ) {
        auto spec_it = params.find(it->first);
        if (spec_it != params.end() && spec_it->second.visual_only) {
            it = merged.params.erase(it);
        } else {
            ++it;
        }
    }

    const auto& domains = spec_domains(definition);
    if (domains.empty()) {
        throw std::runtime_error(
            "Missing domains metadata in component spec for component '" + spec_classname(definition) + "'");
    }
    merged.domains = domains;

    if (merged.priority == "med" && spec_priority(definition) != "med") {
        merged.priority = spec_priority(definition);
    }

    if (!merged.critical && spec_critical(definition)) {
        merged.critical = true;
    }

    if (spec_visual_only(definition)) {
        merged.visual_only = true;
    }

    // Primitive-specific fields
    if (const auto* prim = as_primitive(definition)) {
        merged.execution = prim->execution;
        merged.scheduler_source = prim->scheduler_source;
        merged.solver_owned_electrical = prim->solver_owned_electrical;
        merged.solver_role = prim->solver_role;
    } else {
        merged.scheduler_source = spec_scheduler_source(definition);
        merged.solver_owned_electrical = spec_solver_owned_electrical(definition);
    }

    // Validate params against schema (using unified params map)
    if (!params.empty()) {
        json_parser_internal::validate_params_against_schema(merged.params, params, merged.name, merged.classname);
    }

    return merged;
}

// menu tree, validation, and composite expansion moved to json_parser_model.cpp
