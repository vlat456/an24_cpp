#include "document.h"

#include "signal_key_resolver.h"

#include <nlohmann/json.hpp>
#include <map>
#include <set>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace {

bool split_endpoint(const std::string& endpoint, std::string& node, std::string& port) {
    const size_t dot = endpoint.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= endpoint.size()) {
        return false;
    }
    node = endpoint.substr(0, dot);
    port = endpoint.substr(dot + 1);
    return true;
}

const char* sim_port_type_str(PortType t) {
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

} // namespace

std::string Document::build_simulation_json() const {
    const bp2::Blueprint& bp = model_.current();
    json out = json::object();
    out["templates"] = json::object();

    json devices = json::array();
    std::set<std::string> emitted_ids;
    std::set<std::string> skipped_embedded_proxies;

    for (const bp2::Blueprint::Node& n : bp.nodes()) {
        if (n.expandable) {
            const auto* nested = bp.find_nested(n.id);
            if (nested && nested->embedded) {
                bool has_materialized_children = false;
                const std::string parent_id = std::string(interner_.resolve(n.id));
                for (const auto& child : bp.nodes()) {
                    if (child.group_id == parent_id) {
                        has_materialized_children = true;
                        break;
                    }
                }
                if (has_materialized_children) {
                    skipped_embedded_proxies.insert(parent_id);
                    continue;
                }
            }
        }

        std::string nid = std::string(interner_.resolve(n.id));
        if (!emitted_ids.insert(nid).second) {
            spdlog::warn("[dedup] Duplicate node '{}' on sim export", nid);
            continue;
        }

        json device = json::object();
        device["name"] = nid;
        device["template_name"] = "";
        device["classname"] = std::string(interner_.resolve(n.type));
        if (!n.render_hint.empty()) {
            device["render_hint"] = n.render_hint;
        }
        device["priority"] = "med";
        device["bucket"] = nullptr;
        device["critical"] = false;

        json ports = json::object();
        for (const auto& p : n.inputs) {
            ports[std::string(interner_.resolve(p.name))] = {
                {"direction", "In"},
                {"type", sim_port_type_str(p.type)}
            };
        }
        for (const auto& p : n.outputs) {
            ports[std::string(interner_.resolve(p.name))] = {
                {"direction", "Out"},
                {"type", sim_port_type_str(p.type)}
            };
        }
        device["ports"] = std::move(ports);

        const TypeDefinition* type_def = nullptr;
        std::string classname = std::string(interner_.resolve(n.type));
        if (type_registry_) {
            type_def = type_registry_->get(classname);
        }
        auto is_visual_only = [&](const std::string& key) -> bool {
            if (!type_def) return false;
            auto it = type_def->param_schema.find(key);
            return it != type_def->param_schema.end() && it->second.visual_only;
        };

        auto is_int_param = [&](const std::string& key) -> bool {
            if (!type_def) return false;
            auto it = type_def->param_schema.find(key);
            return it != type_def->param_schema.end() && it->second.type == ParamSchemaType::Int;
        };

        json params = json::object();
        for (const auto& [k, v] : n.params) {
            std::string key = std::string(interner_.resolve(k));
            if (is_visual_only(key)) continue;
            if (is_int_param(key)) {
                params[key] = std::to_string(static_cast<long long>(v));
            } else {
                params[key] = std::to_string(v);
            }
        }
        for (const auto& [k, v] : n.string_params) {
            if (is_visual_only(k)) continue;
            params[k] = v;
        }
        if (!params.empty()) {
            device["params"] = std::move(params);
        }

        devices.push_back(std::move(device));
    }
    out["devices"] = std::move(devices);

    std::map<std::string, std::string> proxy_port_to_bridge;
    if (!skipped_embedded_proxies.empty()) {
        for (const bp2::Blueprint::Node& n : bp.nodes()) {
            std::string nid = std::string(interner_.resolve(n.id));
            for (const auto& proxy_id : skipped_embedded_proxies) {
                if (nid.size() > proxy_id.size() + 1
                    && nid.compare(0, proxy_id.size(), proxy_id) == 0
                    && nid[proxy_id.size()] == ':') {
                    std::string port_name = nid.substr(proxy_id.size() + 1);
                    std::string key = proxy_id + "." + port_name;
                    proxy_port_to_bridge[key] = nid;
                }
            }
        }
    }

    json connections = json::array();
    std::set<std::string> emitted_conn_keys;

    for (const bp2::Blueprint::Wire& w : bp.wires()) {
        auto [src_node, src_port] = bp2_path_to_node_port(w.source);
        auto [tgt_node, tgt_port] = bp2_path_to_node_port(w.target);
        if (src_node.empty() || src_port.empty() || tgt_node.empty() || tgt_port.empty()) {
            continue;
        }

        std::string src_node_s = std::string(interner_.resolve(src_node));
        std::string src_port_s = std::string(interner_.resolve(src_port));
        std::string tgt_node_s = std::string(interner_.resolve(tgt_node));
        std::string tgt_port_s = std::string(interner_.resolve(tgt_port));

        if (skipped_embedded_proxies.count(src_node_s)) {
            auto it = proxy_port_to_bridge.find(src_node_s + "." + src_port_s);
            if (it != proxy_port_to_bridge.end()) {
                src_node_s = it->second;
                src_port_s = "ext";
            } else {
                spdlog::warn("[sim_export] No bridge node found for skipped proxy port '{}.{}'",
                    src_node_s, src_port_s);
            }
        }
        if (skipped_embedded_proxies.count(tgt_node_s)) {
            auto it = proxy_port_to_bridge.find(tgt_node_s + "." + tgt_port_s);
            if (it != proxy_port_to_bridge.end()) {
                tgt_node_s = it->second;
                tgt_port_s = "ext";
            } else {
                spdlog::warn("[sim_export] No bridge node found for skipped proxy port '{}.{}'",
                    tgt_node_s, tgt_port_s);
            }
        }

        const std::string key = src_node_s + "." + src_port_s + "->" + tgt_node_s + "." + tgt_port_s;
        if (!emitted_conn_keys.insert(key).second) {
            spdlog::warn("[dedup] Duplicate connection on sim export: {}", key);
            continue;
        }

        if (!split_endpoint(src_node_s + "." + src_port_s, src_node_s, src_port_s)) {
            continue;
        }
        if (!split_endpoint(tgt_node_s + "." + tgt_port_s, tgt_node_s, tgt_port_s)) {
            continue;
        }

        json conn = json::object();
        conn["from"] = src_node_s + "." + src_port_s;
        conn["to"] = tgt_node_s + "." + tgt_port_s;
        connections.push_back(std::move(conn));
    }
    out["connections"] = std::move(connections);

    return out.dump(2);
}

std::pair<ui::InternedId, ui::InternedId>
Document::bp2_path_to_node_port(const bp2::Path& path) const {
    if (path.kind() != bp2::PathKind::Port) return {};
    ui::InternedId port_name = path.segment();
    bp2::Path parent = arena_.parent(path);
    if (parent.kind() != bp2::PathKind::Node) return {};
    ui::InternedId node_id = parent.segment();
    return {node_id, port_name};
}
