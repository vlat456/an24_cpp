#include "visual/scene/persist.h"
#include "router/router.h"
#include "json_parser/json_parser.h"
#include "data/node.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <optional>
#include <algorithm>
#include <map>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <set>
#include <queue>

using json = nlohmann::json;

static std::string port_type_str(an24::PortType t) {
    switch (t) {
        case an24::PortType::V: return "V";
        case an24::PortType::I: return "I";
        case an24::PortType::Bool: return "Bool";
        case an24::PortType::RPM: return "RPM";
        case an24::PortType::Temperature: return "Temperature";
        case an24::PortType::Pressure: return "Pressure";
        case an24::PortType::Position: return "Position";
        case an24::PortType::Any: return "Any";
    }
    spdlog::error("Unknown PortType enum value: {}", static_cast<int>(t));
    std::abort();
}

// BUGFIX [e2c8d4] Return optional instead of aborting on unknown port type.
// A single malformed blueprint file should not crash the entire application.
static std::optional<an24::PortType> parse_port_type_str(const std::string& s) {
    if (s == "V") return an24::PortType::V;
    if (s == "I") return an24::PortType::I;
    if (s == "Bool") return an24::PortType::Bool;
    if (s == "RPM") return an24::PortType::RPM;
    if (s == "Temperature") return an24::PortType::Temperature;
    if (s == "Pressure") return an24::PortType::Pressure;
    if (s == "Position") return an24::PortType::Position;
    if (s == "Any") return an24::PortType::Any;
    spdlog::error("Unknown port type string: '{}'", s);
    return std::nullopt;
}

/// Serialize ports with InOut detection
static json serialize_ports(const Node& n) {
    std::set<std::string> input_names, output_names;
    for (const auto& p : n.inputs) input_names.insert(p.name);
    for (const auto& p : n.outputs) output_names.insert(p.name);

    json ports = json::object();
    for (const auto& p : n.inputs) {
        bool is_inout = output_names.count(p.name) > 0;
        ports[p.name] = {{"direction", is_inout ? "InOut" : "In"}, {"type", port_type_str(p.type)}};
    }
    for (const auto& p : n.outputs) {
        if (input_names.count(p.name) > 0) continue;
        ports[p.name] = {{"direction", "Out"}, {"type", port_type_str(p.type)}};
    }
    return ports;
}

/// Serialize node_content (UI metadata) to JSON
static json serialize_content(const Node& n) {
    json content = {
        {"type", static_cast<int>(n.node_content.type)},
        {"label", n.node_content.label},
        {"value", n.node_content.value},
        {"min", n.node_content.min},
        {"max", n.node_content.max},
        {"unit", n.node_content.unit}
    };
    if (n.node_content.type == NodeContentType::Switch)
        content["state"] = n.node_content.state;
    return content;
}

// Публичные функции

std::string blueprint_to_json(const Blueprint& bp) {
    json j = json::object();

    // Unified format: devices + connections (simulator format)
    // + editor section for visual debugging metadata

    // templates (empty for now)
    j["templates"] = json::object();

    // devices (simulator format) - full format for AOT/JIT compatibility
    json devices = json::array();
    std::set<std::string> emitted_ids;  // Dedup: skip duplicate node IDs
    for (const auto& n : bp.nodes) {
        // Skip expandable (collapsed) nodes — they are visual-only wrappers.
        // The actual internal devices already exist in the flat blueprint.
        if (n.expandable) continue;

        // BUGFIX [e4a1b7] Skip duplicate node IDs
        if (!emitted_ids.insert(n.id).second) {
            spdlog::warn("[dedup] Duplicate node '{}' on sim export", n.id);
            continue;
        }

        json device = json::object();
        device["name"] = n.id;
        device["template_name"] = "";
        device["classname"] = n.type_name;
        // Store render_hint and expandable so they roundtrip correctly
        if (!n.render_hint.empty())
            device["render_hint"] = n.render_hint;
        if (n.expandable)
            device["expandable"] = true;
        device["priority"] = "med";
        device["bucket"] = nullptr;
        device["critical"] = false;
        // ports
        // BUGFIX [b7e3a9] InOut ports were serialized as "Out" (second loop overwrote first).
        // Now detect InOut by checking if port name exists in both inputs and outputs.
        device["ports"] = serialize_ports(n);
        
        // Add parameters from Node::params (overrides for component defaults)
        if (!n.params.empty()) {
            json params = json::object();
            for (const auto& [key, value] : n.params) {
                params[key] = value;
            }
            device["params"] = params;
        }
        // Add value parameter for RefNode if not already in params (backward compatibility)
        else if (n.type_name == "RefNode") {
            json params = json::object();
            // Use node_content.value if set, otherwise default to 0.0
            float value = (n.node_content.type == NodeContentType::Value) ? n.node_content.value : 0.0f;
            params["value"] = (value == 0.0f) ? "0.0" : std::to_string(value);
            device["params"] = params;
        }

        // [DEAD-g7h8] Removed dead code: was setting blueprint_path on Blueprint-kind nodes,
        // but those are already skipped by `continue` at top of loop.
        
        // NOTE: Other UI params (label, min, max, unit) are stored in editor.nodes, NOT in device.params
        // NOTE: Domains are NOT saved to JSON - they are defined in component definitions
        devices.push_back(device);
    }
    j["devices"] = devices;

    // Build set of expandable node IDs for quick lookup
    std::set<std::string> blueprint_node_ids;
    for (const auto& n : bp.nodes) {
        if (n.expandable)
            blueprint_node_ids.insert(n.id);
    }

    // connections (simulator format)
    // BUGFIX [e4a1b7] Dedup connections in simulator format too
    json connections = json::array();
    std::set<std::string> emitted_conn_keys;
    for (const auto& w : bp.wires) {
        std::string from_node = w.start.node_id;
        std::string from_port = w.start.port_name;
        std::string to_node = w.end.node_id;
        std::string to_port = w.end.port_name;

        // Rewrite wires connected to collapsed Blueprint nodes:
        // e.g. "battery.v_plus → lamp1.vin" becomes "battery.v_plus → lamp1:vin.ext"
        // The internal BlueprintInput/Output node uses the alias "ext" port which
        // union-find merges with "port", connecting external wires to internal circuit.
        bool start_is_bp = blueprint_node_ids.count(from_node) > 0;
        bool end_is_bp = blueprint_node_ids.count(to_node) > 0;

        if (start_is_bp) {
            from_node = from_node + ":" + from_port;
            from_port = "ext";
        }
        if (end_is_bp) {
            to_node = to_node + ":" + to_port;
            to_port = "ext";
        }

        std::string key = from_node + "." + from_port + "→" + to_node + "." + to_port;
        if (!emitted_conn_keys.insert(key).second) {
            spdlog::warn("[dedup] Duplicate connection on sim export: {}", key);
            continue;
        }

        json conn = json::object();
        conn["from"] = from_node + "." + from_port;
        conn["to"] = to_node + "." + to_port;
        connections.push_back(conn);
    }
    j["connections"] = connections;

    // editor section (visual debugging metadata)
    json editor = json::object();
    editor["viewport"] = {
        {"pan", {{"x", bp.pan.x}, {"y", bp.pan.y}}},
        {"zoom", bp.zoom},
        {"grid_step", bp.grid_step}
    };

    // editor.devices: expandable nodes (visual-only, not in simulator devices)
    json editor_devices = json::array();
    for (const auto& n : bp.nodes) {
        if (!n.expandable) continue;
        json device = json::object();
        device["name"] = n.id;
        device["expandable"] = true;
        device["type_name"] = n.type_name;
        if (!n.blueprint_path.empty())
            device["blueprint_path"] = n.blueprint_path;
        json ports = json::object();
        for (const auto& p : n.inputs)
            ports[p.name] = {{"direction", "In"}, {"type", port_type_str(p.type)}};
        for (const auto& p : n.outputs)
            ports[p.name] = {{"direction", "Out"}, {"type", port_type_str(p.type)}};
        device["ports"] = ports;
        editor_devices.push_back(device);
    }
    editor["blueprint_nodes"] = editor_devices;

    // node visual states (position + size + content per device)
    json node_states = json::object();
    for (const auto& n : bp.nodes) {
        json node_state = {
            {"pos", {{"x", n.pos.x}, {"y", n.pos.y}}},
            {"size", {{"x", n.size.x}, {"y", n.size.y}}}
        };

        // Store node_content (UI metadata) in editor.nodes
        if (n.node_content.type != NodeContentType::None) {
            node_state["content"] = serialize_content(n);
        }

        node_states[n.id] = node_state;
    }
    editor["nodes"] = node_states;

    // wire visual states (routing points)
    json wire_states = json::array();
    for (const auto& w : bp.wires) {
        json wire_state = json::object();
        wire_state["from"] = w.start.node_id + "." + w.start.port_name;
        wire_state["to"] = w.end.node_id + "." + w.end.port_name;
        json rps = json::array();
        for (const auto& pt : w.routing_points) {
            rps.push_back({{"x", pt.x}, {"y", pt.y}});
        }
        wire_state["routing_points"] = rps;
        wire_states.push_back(wire_state);
    }
    editor["wires"] = wire_states;

    // sub_blueprint_instances (editor-only visual hierarchy metadata)
    json sub_blueprint_instances = json::array();
    std::set<std::string> saved_group_ids;  // Dedup sub_blueprint_instances
    for (const auto& group : bp.sub_blueprint_instances) {
        if (!saved_group_ids.insert(group.id).second) continue;
        json group_json = {
            {"id", group.id},
            {"blueprint_path", group.blueprint_path},
            {"type_name", group.type_name},
            {"baked_in", group.baked_in},
            {"pos", {{"x", group.pos.x}, {"y", group.pos.y}}},
            {"size", {{"x", group.size.x}, {"y", group.size.y}}},
            {"internal_node_ids", group.internal_node_ids}
        };
        sub_blueprint_instances.push_back(group_json);
    }
    editor["sub_blueprint_instances"] = sub_blueprint_instances;

    j["editor"] = editor;

    return j.dump(2);
}

// ─── Editor save format ────────────────────────────────────────────────────
// Flat list of ALL nodes + wires, no rewriting.
// Hierarchy is metadata only (sub_blueprint_instances), reconstructed on load for visuals.
// blueprint_to_json() is only for simulation (rewrites wires, skips Blueprint kind).

std::string blueprint_to_editor_json(const Blueprint& bp) {
    json j = json::object();

    // devices: flat list of ALL nodes including Blueprint kind
    json devices = json::array();
    std::set<std::string> emitted_ids;  // Dedup: skip duplicate node IDs on save
    for (const auto& n : bp.nodes) {
        if (!emitted_ids.insert(n.id).second) {
            spdlog::warn("[dedup] Duplicate node '{}' on editor save", n.id);
            continue;
        }
        json device = json::object();
        device["name"] = n.id;
        if (n.name != n.id)
            device["display_name"] = n.name;
        device["classname"] = n.type_name;
        if (!n.render_hint.empty())
            device["render_hint"] = n.render_hint;
        if (n.expandable)
            device["expandable"] = true;

        // group_id: use the node's runtime group_id (single source of truth)
        if (!n.group_id.empty())
            device["group_id"] = n.group_id;

        // ports — detect InOut by checking if port appears in both inputs and outputs
        device["ports"] = serialize_ports(n);

        if (!n.params.empty()) {
            json params = json::object();
            for (const auto& [key, value] : n.params)
                params[key] = value;
            device["params"] = params;
        } else if (n.type_name == "RefNode" || n.type_name == "Ref") {
            json params = json::object();
            float value = (n.node_content.type == NodeContentType::Value) ? n.node_content.value : 0.0f;
            params["value"] = (value == 0.0f) ? "0.0" : std::to_string(value);
            device["params"] = params;
        }

        if (!n.blueprint_path.empty())
            device["blueprint_path"] = n.blueprint_path;

        device["pos"] = {{"x", n.pos.x}, {"y", n.pos.y}};
        device["size"] = {{"x", n.size.x}, {"y", n.size.y}};

        // Content (UI metadata)
        if (n.node_content.type != NodeContentType::None) {
            device["content"] = serialize_content(n);
        }

        // Per-node custom color (optional)
        if (n.color.has_value()) {
            device["color"] = {
                {"r", n.color->r},
                {"g", n.color->g},
                {"b", n.color->b},
                {"a", n.color->a}
            };
        }

        devices.push_back(device);
    }
    j["devices"] = devices;

    // wires: flat list, original form, no rewriting
    // BUGFIX [e4a1b7] Dedup wires on save — prevents accumulation of duplicate connections
    json wires = json::array();
    std::set<std::string> emitted_wire_keys;
    for (const auto& w : bp.wires) {
        if (!emitted_ids.count(w.start.node_id) || !emitted_ids.count(w.end.node_id))
            continue;
        std::string key = w.start.node_id + "." + w.start.port_name + "→" +
                          w.end.node_id + "." + w.end.port_name;
        if (!emitted_wire_keys.insert(key).second) {
            spdlog::warn("[dedup] Duplicate wire on save: {}", key);
            continue;
        }
        json wire = json::object();
        wire["from"] = w.start.node_id + "." + w.start.port_name;
        wire["to"] = w.end.node_id + "." + w.end.port_name;
        json rps = json::array();
        for (const auto& pt : w.routing_points)
            rps.push_back({{"x", pt.x}, {"y", pt.y}});
        wire["routing_points"] = rps;
        wires.push_back(wire);
    }
    j["wires"] = wires;

    // sub_blueprint_instances: hierarchy metadata for visual grouping
    json sub_blueprint_instances = json::array();
    std::set<std::string> emitted_group_ids;  // Dedup sub_blueprint_instances
    for (const auto& group : bp.sub_blueprint_instances) {
        if (!emitted_group_ids.insert(group.id).second) continue;
        json gj = {
            {"id", group.id},
            {"blueprint_path", group.blueprint_path},
            {"type_name", group.type_name},
            {"baked_in", group.baked_in},
            {"pos", {{"x", group.pos.x}, {"y", group.pos.y}}},
            {"size", {{"x", group.size.x}, {"y", group.size.y}}},
            {"internal_node_ids", group.internal_node_ids}
        };
        if (!group.params_override.empty()) {
            json po = json::object();
            for (const auto& [k, v] : group.params_override)
                po[k] = v;
            gj["params_override"] = po;
        }
        if (!group.layout_override.empty()) {
            json lo = json::object();
            for (const auto& [k, pt] : group.layout_override)
                lo[k] = {{"x", pt.x}, {"y", pt.y}};
            gj["layout_override"] = lo;
        }
        if (!group.internal_routing.empty()) {
            json ir = json::object();
            for (const auto& [k, pts] : group.internal_routing) {
                json arr = json::array();
                for (const auto& pt : pts)
                    arr.push_back({{"x", pt.x}, {"y", pt.y}});
                ir[k] = arr;
            }
            gj["internal_routing"] = ir;
        }
        sub_blueprint_instances.push_back(gj);
    }
    j["sub_blueprint_instances"] = sub_blueprint_instances;

    // viewport
    j["viewport"] = {
        {"pan", {{"x", bp.pan.x}, {"y", bp.pan.y}}},
        {"zoom", bp.zoom},
        {"grid_step", bp.grid_step}
    };

    return j.dump(2);
}

// BUGFIX [f2c8d4] Removed ~200 lines of dead code:
// - device_to_node() — legacy JSON→Node converter, unused since editor format migration
// - device_instance_to_node() — DeviceInstance→Node converter, no callers
// - create_node_content() — duplicate wrapper, replaced by create_node_content_from_def
// - auto_layout() / port_center() / classify_node() / NodeRole — never called
// - snap() / LAYOUT_GRID — only used by dead auto_layout

// Разобрать "device.port" на component и port
static bool parse_port_ref(const std::string& ref, std::string& device, std::string& port) {
    size_t dot = ref.find('.');
    if (dot == std::string::npos) return false;
    device = ref.substr(0, dot);
    port = ref.substr(dot + 1);
    return true;
}

// ─── Apply port types from component registry ──────────────────────────────

static void apply_port_types_from_registry(Node& n, const an24::TypeRegistry& registry) {
    const auto* def = registry.get(n.type_name);
    if (!def) return;
    for (auto& p : n.inputs) {
        auto it = def->ports.find(p.name);
        if (it != def->ports.end()) p.type = it->second.type;
    }
    for (auto& p : n.outputs) {
        auto it = def->ports.find(p.name);
        if (it != def->ports.end()) p.type = it->second.type;
    }
}

/// Fill missing params from component definition defaults (same merge as merge_device_instance)
static void apply_params_from_registry(Node& n, const an24::TypeRegistry& registry) {
    const auto* def = registry.get(n.type_name);
    if (!def) return;
    for (const auto& [key, value] : def->params) {
        if (n.params.find(key) == n.params.end()) {
            n.params[key] = value;
        }
    }
}

// ─── Load: editor format (flat devices + wires + sub_blueprint_instances) ─────

static std::optional<Blueprint> load_editor_format(const json& j) {
    Blueprint bp;

    // Load devices → nodes (all inline: pos, size, kind, content, group_id)
    std::set<std::string> loaded_ids;  // Dedup: skip duplicate node IDs
    for (const auto& d : j["devices"]) {
        Node n;
        n.id = d.value("name", "");
        n.name = d.value("display_name", n.id);
        n.type_name = d.value("classname", "");

        // BUGFIX [e4a1b7] Skip duplicate node IDs (malformed saves may contain duplicates)
        if (!loaded_ids.insert(n.id).second) {
            spdlog::warn("[dedup] Duplicate node '{}' found on load — skipping", n.id);
            continue;
        }

        n.render_hint = d.value("render_hint", "");
        n.expandable = d.value("expandable", false);

        // Ports
        if (d.contains("ports") && d["ports"].is_object()) {
            for (auto& [port_name, port_info] : d["ports"].items()) {
                Port p;
                p.name = port_name;
                if (port_info.contains("type")) {
                    auto pt = parse_port_type_str(port_info["type"].get<std::string>());
                    if (!pt) {
                        spdlog::warn("Port '{}' on device '{}': unknown type '{}', defaulting to Any",
                                     port_name, n.id, port_info["type"].get<std::string>());
                        p.type = an24::PortType::Any;
                    } else {
                        p.type = *pt;
                    }
                } else {
                    spdlog::warn("Port '{}' on device '{}' missing 'type' field in JSON, defaulting to Any",
                                 port_name, n.id);
                    p.type = an24::PortType::Any;
                }
                std::string dir = port_info.value("direction", "In");
                if (dir == "Out" || dir == "InOut") {
                    p.side = PortSide::Output;
                    n.outputs.push_back(p);
                }
                if (dir == "In" || dir == "InOut") {
                    p.side = PortSide::Input;
                    n.inputs.push_back(p);
                }
            }
        }

        // Params
        if (d.contains("params") && d["params"].is_object()) {
            for (auto& [key, val] : d["params"].items()) {
                n.params[key] = val.is_string() ? val.get<std::string>() : val.dump();
            }
        }

        // Blueprint path
        if (d.contains("blueprint_path")) {
            n.blueprint_path = d["blueprint_path"].get<std::string>();
            if (n.expandable)
                n.collapsed = true;
        }

        // Position + size (inline)
        if (d.contains("pos")) {
            n.pos.x = d["pos"].value("x", 0.0f);
            n.pos.y = d["pos"].value("y", 0.0f);
        }
        if (d.contains("size")) {
            n.size.x = d["size"].value("x", 120.0f);
            n.size.y = d["size"].value("y", 80.0f);
            n.size_explicitly_set = true;
        }

        // Content (UI metadata)
        if (d.contains("content")) {
            const auto& c = d["content"];
            if (c.contains("type"))
                n.node_content.type = static_cast<NodeContentType>(c["type"].get<int>());
            if (c.contains("label"))
                n.node_content.label = c["label"].get<std::string>();
            if (c.contains("value"))
                n.node_content.value = c["value"].get<float>();
            if (c.contains("min"))
                n.node_content.min = c["min"].get<float>();
            if (c.contains("max"))
                n.node_content.max = c["max"].get<float>();
            if (c.contains("unit"))
                n.node_content.unit = c["unit"].get<std::string>();
            if (c.contains("state"))
                n.node_content.state = c["state"].get<bool>();
        }

        // Per-node custom color (optional)
        if (d.contains("color") && d["color"].is_object()) {
            NodeColor c;
            c.r = d["color"].value("r", 0.5f);
            c.g = d["color"].value("g", 0.5f);
            c.b = d["color"].value("b", 0.5f);
            c.a = d["color"].value("a", 1.0f);
            n.color = c;
        }

        bp.nodes.push_back(std::move(n));
    }

    // [PERF-q7r8] Was reloading component registry from disk on every load — now cached as static
    static an24::TypeRegistry registry = an24::load_type_registry();
    for (auto& n : bp.nodes) {
        apply_port_types_from_registry(n, registry);
        apply_params_from_registry(n, registry);
        // Enrich render_hint from registry for old saves that predate the migration
        if (n.render_hint.empty()) {
            const auto* def = registry.get(n.type_name);
            if (def) n.render_hint = def->render_hint;
        }
    }

    // BUGFIX [e4a1b7] Load wires with dedup — reject duplicate connections on load
    std::set<std::string> loaded_wire_keys;
    for (const auto& ws : j["wires"]) {
        std::string from = ws.value("from", "");
        std::string to = ws.value("to", "");

        std::string from_device, from_port, to_device, to_port;
        if (!parse_port_ref(from, from_device, from_port) ||
            !parse_port_ref(to, to_device, to_port))
            continue;

        std::string key = from + "→" + to;
        if (!loaded_wire_keys.insert(key).second) {
            spdlog::warn("[dedup] Duplicate wire on load: {} → {}", from, to);
            continue;
        }

        Wire w;
        w.id = key;
        w.start.node_id = from_device;
        w.start.port_name = from_port;
        w.end.node_id = to_device;
        w.end.port_name = to_port;

        if (ws.contains("routing_points") && ws["routing_points"].is_array()) {
            std::set<std::pair<float,float>> seen_rps;
            for (const auto& pj : ws["routing_points"]) {
                float rx = pj.value("x", 0.0f), ry = pj.value("y", 0.0f);
                if (!seen_rps.insert({rx, ry}).second) {
                    spdlog::warn("[dedup] Duplicate routing point ({},{}) in wire {}", rx, ry, key);
                    continue;
                }
                w.routing_points.push_back(Pt(rx, ry));
            }
        }

        bp.wires.push_back(std::move(w));
    }

    // [2.1] Rebuild wire dedup index after loading all wires
    bp.rebuild_wire_index();

    // Reconstruct sub_blueprint_instances from group metadata + group_id on devices
    if (j.contains("sub_blueprint_instances") && j["sub_blueprint_instances"].is_array()) {
        // Build map of group_id → internal node IDs from device group_id tags
        // Use sets to dedup (malformed saves may have duplicate devices)
        std::map<std::string, std::set<std::string>> group_member_sets;
        for (const auto& d : j["devices"]) {
            if (d.contains("group_id")) {
                group_member_sets[d["group_id"].get<std::string>()].insert(
                    d.value("name", ""));
            }
        }
        std::map<std::string, std::vector<std::string>> group_members;
        for (auto& [gid, members] : group_member_sets)
            group_members[gid] = std::vector<std::string>(members.begin(), members.end());

        std::set<std::string> loaded_group_ids;  // Dedup sub_blueprint_instances by ID
        for (const auto& gj : j["sub_blueprint_instances"]) {
            SubBlueprintInstance group;
            group.id = gj.value("id", "");
            if (!loaded_group_ids.insert(group.id).second) continue;
            group.blueprint_path = gj.value("blueprint_path", "");
            group.type_name = gj.value("type_name", "");
            group.baked_in = gj.value("baked_in", false);

            // Reconstruct internal_node_ids from group_id tags on devices
            auto it = group_members.find(group.id);
            if (it != group_members.end())
                group.internal_node_ids = it->second;

            // Backward compat: if saved with explicit internal_node_ids, use those
            if (group.internal_node_ids.empty() &&
                gj.contains("internal_node_ids") && gj["internal_node_ids"].is_array()) {
                for (const auto& nid : gj["internal_node_ids"])
                    group.internal_node_ids.push_back(nid.get<std::string>());
            }

            // Restore params_override, layout_override, internal_routing
            if (gj.contains("params_override") && gj["params_override"].is_object()) {
                for (auto& [k, v] : gj["params_override"].items())
                    group.params_override[k] = v.get<std::string>();
            }
            if (gj.contains("layout_override") && gj["layout_override"].is_object()) {
                for (auto& [k, v] : gj["layout_override"].items())
                    group.layout_override[k] = Pt(v.value("x", 0.0f), v.value("y", 0.0f));
            }
            if (gj.contains("internal_routing") && gj["internal_routing"].is_object()) {
                for (auto& [k, arr] : gj["internal_routing"].items()) {
                    std::vector<Pt> pts;
                    if (arr.is_array()) {
                        for (const auto& p : arr)
                            pts.push_back(Pt(p.value("x", 0.0f), p.value("y", 0.0f)));
                    }
                    group.internal_routing[k] = pts;
                }
            }

            // Get pos/size from the expandable node (single source of truth)
            bool found_node = false;
            for (const auto& n : bp.nodes) {
                if (n.id == group.id && n.expandable) {
                    group.pos = n.pos;
                    group.size = n.size;
                    found_node = true;
                    break;
                }
            }
            // Fallback: use saved pos/size from JSON (reference-mode instances
            // may not have a corresponding expandable node yet)
            if (!found_node && gj.contains("pos")) {
                group.pos.x = gj["pos"].value("x", 0.0f);
                group.pos.y = gj["pos"].value("y", 0.0f);
            }
            if (!found_node && gj.contains("size")) {
                group.size.x = gj["size"].value("x", 120.0f);
                group.size.y = gj["size"].value("y", 80.0f);
            }

            bp.sub_blueprint_instances.push_back(group);
        }
    }

    // Viewport
    if (j.contains("viewport")) {
        const auto& vp = j["viewport"];
        if (vp.contains("pan")) {
            bp.pan.x = vp["pan"].value("x", 0.0f);
            bp.pan.y = vp["pan"].value("y", 0.0f);
        }
        bp.zoom = vp.value("zoom", 1.0f);
        bp.grid_step = vp.value("grid_step", 16.0f);
    }

    // Initialize next_wire_id
    bp.next_wire_id = static_cast<int>(bp.wires.size());
    for (const auto& w : bp.wires) {
        if (w.id.compare(0, 5, "wire_") == 0) {
            int num = std::atoi(w.id.c_str() + 5);
            if (num >= bp.next_wire_id) bp.next_wire_id = num + 1;
        }
    }

    bp.recompute_group_ids();
    return bp;
}

std::optional<Blueprint> blueprint_from_json(const std::string& json_str) {
    try {
        json j = json::parse(json_str);

        if (!j.contains("devices") || !j["devices"].is_array())
            return std::nullopt;

        if (!j.contains("wires") || !j["wires"].is_array()) {
            spdlog::error("blueprint_from_json: missing 'wires' array (legacy format no longer supported)");
            return std::nullopt;
        }

        return load_editor_format(j);
    } catch (const std::exception& e) {
        spdlog::error("blueprint_from_json failed: {}", e.what());
        return std::nullopt;
    }
}

bool save_blueprint_to_file(const Blueprint& bp, const char* path) {
    // BUGFIX [d9c3f2] Protect embedded blueprint originals from accidental overwrite
    namespace fs = std::filesystem;
    fs::path save_path = fs::weakly_canonical(fs::path(path));
    // Reject saving into any directory named "library" to protect type definitions
    for (auto it = save_path.begin(); it != save_path.end(); ++it) {
        if (*it == "library") {
            spdlog::error("Refusing to save into library/ directory: {}", path);
            return false;
        }
    }
    std::string json_str = blueprint_to_editor_json(bp);
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << json_str;
    return true;
}

std::optional<Blueprint> load_blueprint_from_file(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return blueprint_from_json(buffer.str());
}
