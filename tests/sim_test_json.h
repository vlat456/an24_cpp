#pragma once

#include "editor/data/blueprint.h"
#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace sim_test_json {

inline const char* port_type_str(PortType t) {
    switch (t) {
        case PortType::V: return "V";
        case PortType::I: return "I";
        case PortType::Bool: return "Bool";
        case PortType::RPM: return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure: return "Pressure";
        case PortType::Position: return "Position";
        case PortType::Any:
        default: return "Any";
    }
}

inline std::string from_blueprint(const Blueprint& bp) {
    nlohmann::json out = nlohmann::json::object();
    out["templates"] = nlohmann::json::object();

    nlohmann::json devices = nlohmann::json::array();
    std::set<std::string> emitted_ids;

    for (const auto& n : bp.nodes) {
        if (n.expandable) continue;

        std::string id = std::string(bp.interner().resolve(n.id));
        if (!emitted_ids.insert(id).second) continue;

        nlohmann::json d = nlohmann::json::object();
        d["name"] = id;
        d["template_name"] = "";
        d["classname"] = n.type_name;
        if (!n.render_hint.empty()) d["render_hint"] = n.render_hint;
        d["priority"] = "med";
        d["bucket"] = nullptr;
        d["critical"] = false;

        nlohmann::json ports = nlohmann::json::object();
        for (const auto& p : n.inputs) {
            ports[std::string(bp.interner().resolve(p.name))] = {
                {"direction", "In"},
                {"type", port_type_str(p.type)}
            };
        }
        for (const auto& p : n.outputs) {
            ports[std::string(bp.interner().resolve(p.name))] = {
                {"direction", "Out"},
                {"type", port_type_str(p.type)}
            };
        }
        d["ports"] = std::move(ports);

        nlohmann::json params = nlohmann::json::object();
        for (const auto& [k, v] : n.params) {
            params[k] = v;
        }
        if (!params.empty()) d["params"] = std::move(params);

        devices.push_back(std::move(d));
    }

    out["devices"] = std::move(devices);

    std::set<std::string> blueprint_ids;
    for (const auto& n : bp.nodes) {
        if (n.expandable) {
            blueprint_ids.insert(std::string(bp.interner().resolve(n.id)));
        }
    }

    nlohmann::json connections = nlohmann::json::array();
    std::set<std::string> emitted_conn;

    for (const auto& w : bp.wires) {
        std::string src_n = std::string(bp.interner().resolve(w.start.node_id));
        std::string src_p = std::string(bp.interner().resolve(w.start.port_name));
        std::string dst_n = std::string(bp.interner().resolve(w.end.node_id));
        std::string dst_p = std::string(bp.interner().resolve(w.end.port_name));

        if (blueprint_ids.count(src_n) > 0) {
            src_n = src_n + ":" + src_p;
            src_p = "ext";
        }
        if (blueprint_ids.count(dst_n) > 0) {
            dst_n = dst_n + ":" + dst_p;
            dst_p = "ext";
        }

        std::string key = src_n + "." + src_p + "->" + dst_n + "." + dst_p;
        if (!emitted_conn.insert(key).second) continue;

        connections.push_back({
            {"from", src_n + "." + src_p},
            {"to", dst_n + "." + dst_p}
        });
    }

    out["connections"] = std::move(connections);
    return out.dump(2);
}

} // namespace sim_test_json
