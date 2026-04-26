#include "io/json/type_definition_json.h"

#include "io/json/json_internal_utils.h"
#include "io/json/json_parse_internal.h"

#include <algorithm>

using json = nlohmann::json;

namespace {

bp2::Direction parse_port_direction(const std::string& s) {
    if (s == "in" || s == "input" || s == "i" || s == "In") return bp2::Direction::Input;
    if (s == "InOut" || s == "inout" || s == "io") return bp2::Direction::InOut;
    if (s == "out" || s == "output" || s == "o" || s == "Out") return bp2::Direction::Output;
    throw std::runtime_error("Invalid port direction '" + s + "'");
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
            port.type = json_io_internal::parse_port_type_string(j["type"].get<std::string>());
        } else {
            throw std::runtime_error("Port definition missing required 'type' field");
        }
        if (j.contains("domain")) {
            if (!j["domain"].is_number_integer()) {
                throw std::runtime_error("Port definition field 'domain' must be integer bitmask");
            }
            port.domain = json_io_internal::parse_domain_mask_int(j["domain"].get<int>());
        } else {
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

} // namespace

namespace json_io_detail {

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
        for (auto& [key, value] : j["params"].items()) {
            dev.params[key] = value.get<std::string>();
        }
    }

    if (j.contains("pos") && j["pos"].is_object()) {
        auto& p = j["pos"];
        if (p.contains("x") && p.contains("y")) {
            dev.pos = {p["x"].get<float>(), p["y"].get<float>()};
        }
    }
    if (j.contains("size") && j["size"].is_object()) {
        auto& s = j["size"];
        if (s.contains("x") && s.contains("y")) {
            dev.size = {s["x"].get<float>(), s["y"].get<float>()};
        }
    }

    return dev;
}

} // namespace json_io_detail

std::pair<ComponentSpec, TypePresentation> parse_type_definition(const json& j) {
    TypePresentation pres;

    std::string classname;
    if (j.contains("classname")) classname = j["classname"].get<std::string>();
    else throw std::runtime_error("Type definition missing 'classname' field");

    if (j.contains("description")) pres.description = j["description"].get<std::string>();
    if (j.contains("content_type")) pres.content_type = bp2::parse_node_content_type(j["content_type"].get<std::string_view>());
    if (j.contains("render_hint")) pres.render_hint = j["render_hint"].get<std::string>();

    if (j.contains("size") && j["size"].is_object()) {
        auto size_obj = j["size"];
        if (size_obj.contains("x") && size_obj.contains("y")) {
            pres.default_size = {size_obj["x"].get<float>(), size_obj["y"].get<float>()};
        }
    }

    ComponentMeta meta;
    meta.classname = classname;
    if (j.contains("priority")) meta.priority = j["priority"].get<std::string>();
    if (j.contains("critical")) meta.critical = j["critical"].get<bool>();
    // visual_only moved to TypePresentation
    if (j.contains("visual_only")) pres.visual_only = j["visual_only"].get<bool>();
    // scheduler_role: typed enum parsed from string
    SchedulerRoleKind scheduler_role_kind = SchedulerRoleKind::Consumer;
    if (j.contains("scheduler_role")) {
        scheduler_role_kind = parse_scheduler_role_kind(j["scheduler_role"].get<std::string>());
    }

    if (j.contains("ports")) {
        for (auto& [port_name, port_val] : j["ports"].items()) {
            meta.ports[port_name] = parse_port(port_val);
        }
    }

    json_io_internal::merge_params_and_schema(j, "params", meta.params);

    if (!j.contains("domains") || !j["domains"].is_array()) {
        throw std::runtime_error("Type definition missing required 'domains' array for component '" + classname + "'");
    }
    for (const auto& d : j["domains"]) {
        meta.domains.push_back(json_io_internal::parse_domain_string(d.get<std::string>()));
    }
    if (meta.domains.empty()) {
        throw std::runtime_error("Type definition has empty 'domains' array for component '" + classname + "'");
    }

    const bool is_cpp_class = j.value("cpp_class", false);
    ComponentSpec spec;

    if (is_cpp_class) {
        PrimitiveSpec prim;
        static_cast<ComponentMeta&>(prim) = std::move(meta);

        // Set solver metadata from fields that were hoisted to meta-level
        prim.solver.scheduler_role_kind = scheduler_role_kind;

        if (j.contains("solver_role") && j["solver_role"].is_object()) {
            SolverRole role;
            const auto& sr = j["solver_role"];
            if (!sr.contains("kind") || !sr["kind"].is_string()) {
                throw std::runtime_error("solver_role missing required string 'kind' for component '" + classname + "'");
            }
            role.kind = parse_solver_role_kind(sr["kind"].get<std::string>());
            if (sr.contains("domain") && sr["domain"].is_string()) {
                role.domain = json_io_internal::parse_domain_string(sr["domain"].get<std::string>());
            }
            if (sr.contains("port_map") && sr["port_map"].is_object()) {
                for (auto& [key, value] : sr["port_map"].items()) {
                    role.port_map[key] = value.get<std::string>();
                }
            }
            if (sr.contains("param_map") && sr["param_map"].is_object()) {
                for (auto& [key, value] : sr["param_map"].items()) {
                    role.param_map[key] = value.get<std::string>();
                }
            }
            if (sr.contains("value_map") && sr["value_map"].is_object()) {
                for (auto& [key, value] : sr["value_map"].items()) {
                    role.value_map[key] = value.get<float>();
                }
            }
            prim.solver.solver_role = role;
        }

        spec = std::move(prim);
    } else {
        CompositeSpec comp;
        static_cast<ComponentMeta&>(comp) = std::move(meta);

        if (j.contains("devices") && j["devices"].is_array()) {
            for (const auto& dev_j : j["devices"]) {
                comp.devices.push_back(json_io_detail::parse_device(dev_j));
            }
        }
        if (j.contains("connections") && j["connections"].is_array()) {
            for (const auto& conn_j : j["connections"]) {
                RoutedConnection rc;
                static_cast<Connection&>(rc) = json_io_detail::parse_connection(conn_j);
                comp.connections.push_back(std::move(rc));
            }
        }
        if (comp.connections.empty() && j.contains("wires") && j["wires"].is_array()) {
            for (const auto& wire_j : j["wires"]) {
                RoutedConnection conn;
                Connection base = json_io_detail::parse_connection(wire_j);
                static_cast<Connection&>(conn) = std::move(base);
                if (wire_j.contains("routing_points") && wire_j["routing_points"].is_array()) {
                    for (const auto& rp : wire_j["routing_points"]) {
                        if (rp.contains("x") && rp.contains("y")) {
                            conn.routing_points.push_back({rp["x"].get<float>(), rp["y"].get<float>()});
                        }
                    }
                }
                comp.connections.push_back(std::move(conn));
            }
        }

        if (j.contains("sub_blueprints") && j["sub_blueprints"].is_array()) {
            for (const auto& sbj : j["sub_blueprints"]) {
                SubBlueprintRef ref;
                ref.id = sbj.value("id", "");
                ref.blueprint_path = sbj.value("blueprint_path", "");
                ref.type_name = sbj.value("type_name", "");
                if (sbj.contains("pos")) {
                    ref.pos = {sbj["pos"].value("x", 0.0f), sbj["pos"].value("y", 0.0f)};
                }
                if (sbj.contains("size")) {
                    ref.size = {sbj["size"].value("x", 0.0f), sbj["size"].value("y", 0.0f)};
                }
                if (sbj.contains("params_override") && sbj["params_override"].is_object()) {
                    for (auto& [key, value] : sbj["params_override"].items()) {
                        ref.params_override[key] = value.get<std::string>();
                    }
                }
                comp.sub_blueprints.push_back(std::move(ref));
            }
        }

        spec = std::move(comp);
    }

    return {spec, pres};
}
