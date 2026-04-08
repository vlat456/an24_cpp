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

void collect_nested_devices_recursive(
    const bp2::Blueprint& inline_bp,
    const TypeRegistry* type_registry,
    const ui::StringInterner& interner,
    std::set<std::string>& emitted_ids,
    json& devices,
    std::map<std::string, std::string>& proxy_port_to_bridge,
    const std::string& parent_collapsed_id) {
    for (const bp2::Blueprint::Node& in : inline_bp.nodes()) {
        const std::string local_id = std::string(interner.resolve(in.semantic.id));
        const std::string exported_id = parent_collapsed_id + ":" + local_id;
        if (!emitted_ids.insert(exported_id).second) {
            spdlog::warn("[dedup] Duplicate node '{}' in nested expansion", exported_id);
            continue;
        }

        json device = json::object();
        device["name"] = exported_id;
        device["template_name"] = "";
        device["classname"] = std::string(interner.resolve(in.semantic.type));
        if (!in.view.render_hint.empty()) {
            device["render_hint"] = in.view.render_hint;
        }
        device["priority"] = "med";
        device["bucket"] = nullptr;
        device["critical"] = false;

        json ports = json::object();
        for (const auto& p : in.view.inputs) {
            ports[std::string(interner.resolve(p.name))] = {
                {"direction", "In"},
                {"type", sim_port_type_str(p.type)}
            };
        }
        for (const auto& p : in.view.outputs) {
            ports[std::string(interner.resolve(p.name))] = {
                {"direction", "Out"},
                {"type", sim_port_type_str(p.type)}
            };
        }
        device["ports"] = std::move(ports);

        const TypeDefinition* type_def = nullptr;
        std::string classname = std::string(interner.resolve(in.semantic.type));
        if (type_registry) {
            type_def = type_registry->get(classname);
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
        for (const auto& [k, v] : in.semantic.params) {
            std::string key = std::string(interner.resolve(k));
            if (is_visual_only(key)) continue;
            if (is_int_param(key)) {
                params[key] = std::to_string(static_cast<long long>(v));
            } else {
                params[key] = std::to_string(v);
            }
        }
        for (const auto& [k, v] : in.semantic.string_params) {
            if (is_visual_only(k)) continue;
            params[k] = v;
        }
        if (!params.empty()) {
            device["params"] = std::move(params);
        }

        devices.push_back(std::move(device));
    }

    const auto bp_input_iid = interner.lookup("BlueprintInput");
    const auto bp_output_iid = interner.lookup("BlueprintOutput");
    for (const bp2::Blueprint::Node& in : inline_bp.nodes()) {
        if (!bp_input_iid.empty() && in.semantic.type == bp_input_iid) {
            const std::string bridge_local_id = std::string(interner.resolve(in.semantic.id));
            proxy_port_to_bridge[parent_collapsed_id + "." + bridge_local_id] = parent_collapsed_id + ":" + bridge_local_id;
        } else if (!bp_output_iid.empty() && in.semantic.type == bp_output_iid) {
            const std::string bridge_local_id = std::string(interner.resolve(in.semantic.id));
            proxy_port_to_bridge[parent_collapsed_id + "." + bridge_local_id] = parent_collapsed_id + ":" + bridge_local_id;
        }
    }

    for (const auto& nested_child : inline_bp.nested()) {
        if (nested_child.embedded && nested_child.inline_def) {
            collect_nested_devices_recursive(
                *nested_child.inline_def,
                type_registry,
                interner,
                emitted_ids,
                devices,
                proxy_port_to_bridge,
                parent_collapsed_id + ":" + std::string(interner.resolve(nested_child.id)));
        }
    }
}

void collect_nested_connections_recursive(
    const bp2::Blueprint& inline_bp,
    const bp2::PathArena& arena,
    const ui::StringInterner& interner,
    std::set<std::string>& emitted_conn_keys,
    json& connections,
    const std::string& parent_collapsed_id) {
    for (const bp2::Blueprint::Wire& w : inline_bp.wires()) {
        if (w.source.kind() != bp2::PathKind::Port || w.target.kind() != bp2::PathKind::Port) {
            continue;
        }
        const bp2::Path src_parent = arena.parent(w.source);
        const bp2::Path tgt_parent = arena.parent(w.target);
        if (src_parent.kind() != bp2::PathKind::Node || tgt_parent.kind() != bp2::PathKind::Node) {
            continue;
        }

        std::string src_node_s = parent_collapsed_id + ":" + std::string(interner.resolve(src_parent.segment()));
        std::string src_port_s = std::string(interner.resolve(w.source.segment()));
        std::string tgt_node_s = parent_collapsed_id + ":" + std::string(interner.resolve(tgt_parent.segment()));
        std::string tgt_port_s = std::string(interner.resolve(w.target.segment()));

        const std::string key = src_node_s + "." + src_port_s + "->" + tgt_node_s + "." + tgt_port_s;
        if (!emitted_conn_keys.insert(key).second) {
            spdlog::warn("[dedup] Duplicate connection on sim export: {}", key);
            continue;
        }

        json conn = json::object();
        conn["from"] = src_node_s + "." + src_port_s;
        conn["to"] = tgt_node_s + "." + tgt_port_s;
        connections.push_back(std::move(conn));
    }

    for (const auto& nested_child : inline_bp.nested()) {
        if (nested_child.embedded && nested_child.inline_def) {
            collect_nested_connections_recursive(
                *nested_child.inline_def,
                arena,
                interner,
                emitted_conn_keys,
                connections,
                parent_collapsed_id + ":" + std::string(interner.resolve(nested_child.id)));
        }
    }
}

} // namespace

std::string Document::build_simulation_json() const {
    const bp2::Blueprint& bp = model_.current();
    json out = json::object();
    out["templates"] = json::object();

    json devices = json::array();
    std::set<std::string> emitted_ids;
    std::map<std::string, std::string> proxy_port_to_bridge;

    for (const bp2::Blueprint::Node& n : bp.nodes()) {
        if (n.view.expandable) {
            const auto* nested = bp.find_nested(n.semantic.id);
            if (nested && nested->embedded && nested->inline_def) {
                std::string parent_id = std::string(interner_.resolve(n.semantic.id));
                collect_nested_devices_recursive(
                    *nested->inline_def,
                    type_registry_,
                    interner_,
                    emitted_ids,
                    devices,
                    proxy_port_to_bridge,
                    parent_id);
                continue;
            }
        }

        std::string nid = std::string(interner_.resolve(n.semantic.id));
        if (!emitted_ids.insert(nid).second) {
            spdlog::warn("[dedup] Duplicate node '{}' on sim export", nid);
            continue;
        }

        json device = json::object();
        device["name"] = nid;
        device["template_name"] = "";
        device["classname"] = std::string(interner_.resolve(n.semantic.type));
        if (!n.view.render_hint.empty()) {
            device["render_hint"] = n.view.render_hint;
        }
        device["priority"] = "med";
        device["bucket"] = nullptr;
        device["critical"] = false;

        json ports = json::object();
        for (const auto& p : n.view.inputs) {
            ports[std::string(interner_.resolve(p.name))] = {
                {"direction", "In"},
                {"type", sim_port_type_str(p.type)}
            };
        }
        for (const auto& p : n.view.outputs) {
            ports[std::string(interner_.resolve(p.name))] = {
                {"direction", "Out"},
                {"type", sim_port_type_str(p.type)}
            };
        }
        device["ports"] = std::move(ports);

        const TypeDefinition* type_def = nullptr;
        std::string classname = std::string(interner_.resolve(n.semantic.type));
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
        for (const auto& [k, v] : n.semantic.params) {
            std::string key = std::string(interner_.resolve(k));
            if (is_visual_only(key)) continue;
            if (is_int_param(key)) {
                params[key] = std::to_string(static_cast<long long>(v));
            } else {
                params[key] = std::to_string(v);
            }
        }
        for (const auto& [k, v] : n.semantic.string_params) {
            if (is_visual_only(k)) continue;
            params[k] = v;
        }
        if (!params.empty()) {
            device["params"] = std::move(params);
        }

        devices.push_back(std::move(device));
    }
    out["devices"] = std::move(devices);

    json connections = json::array();
    std::set<std::string> emitted_conn_keys;

    for (const bp2::Blueprint::Node& n : bp.nodes()) {
        if (n.view.expandable) {
            const auto* nested = bp.find_nested(n.semantic.id);
            if (nested && nested->embedded && nested->inline_def) {
                collect_nested_connections_recursive(
                    *nested->inline_def,
                    arena_,
                    interner_,
                    emitted_conn_keys,
                    connections,
                    std::string(interner_.resolve(n.semantic.id)));
                continue;
            }
        }
    }

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

        if (proxy_port_to_bridge.count(src_node_s + "." + src_port_s)) {
            auto it = proxy_port_to_bridge.find(src_node_s + "." + src_port_s);
            if (it != proxy_port_to_bridge.end()) {
                src_node_s = it->second;
                src_port_s = "ext";
            }
        }
        if (proxy_port_to_bridge.count(tgt_node_s + "." + tgt_port_s)) {
            auto it = proxy_port_to_bridge.find(tgt_node_s + "." + tgt_port_s);
            if (it != proxy_port_to_bridge.end()) {
                tgt_node_s = it->second;
                tgt_port_s = "ext";
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
