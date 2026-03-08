#include "persist.h"
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
        default: return "Any";
    }
}

static an24::PortType parse_port_type_str(const std::string& s) {
    if (s == "V") return an24::PortType::V;
    if (s == "I") return an24::PortType::I;
    if (s == "Bool") return an24::PortType::Bool;
    if (s == "RPM") return an24::PortType::RPM;
    if (s == "Temperature") return an24::PortType::Temperature;
    if (s == "Pressure") return an24::PortType::Pressure;
    if (s == "Position") return an24::PortType::Position;
    return an24::PortType::Any;
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
        // Skip collapsed Blueprint nodes — they are visual-only wrappers.
        // The actual internal devices already exist in the flat blueprint.
        if (n.kind == NodeKind::Blueprint) continue;

        // Skip duplicate node IDs (malformed saves may contain duplicates)
        if (!emitted_ids.insert(n.id).second) continue;

        json device = json::object();
        device["name"] = n.id;
        device["template_name"] = "";
        device["classname"] = n.type_name;
        // Store NodeKind explicitly so it roundtrips correctly
        switch (n.kind) {
            case NodeKind::Bus: device["kind"] = "Bus"; break;
            case NodeKind::Ref: device["kind"] = "Ref"; break;
            case NodeKind::Blueprint: device["kind"] = "Blueprint"; break;
            default: device["kind"] = "Node"; break;
        }
        device["priority"] = "med";
        device["bucket"] = nullptr;
        device["critical"] = false;
        // ports
        json ports = json::object();
        for (const auto& p : n.inputs) {
            ports[p.name] = {{"direction", "In"}, {"type", port_type_str(p.type)}};
        }
        for (const auto& p : n.outputs) {
            ports[p.name] = {{"direction", "Out"}, {"type", port_type_str(p.type)}};
        }
        device["ports"] = ports;
        
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

        // Store blueprint_path for collapsed blueprint nodes
        if (n.kind == NodeKind::Blueprint && !n.blueprint_path.empty()) {
            device["blueprint_path"] = n.blueprint_path;
        }
        
        // NOTE: Other UI params (label, min, max, unit) are stored in editor.nodes, NOT in device.params
        // NOTE: Domains are NOT saved to JSON - they are defined in component definitions
        devices.push_back(device);
    }
    j["devices"] = devices;

    // Build set of Blueprint node IDs for quick lookup
    std::set<std::string> blueprint_node_ids;
    for (const auto& n : bp.nodes) {
        if (n.kind == NodeKind::Blueprint)
            blueprint_node_ids.insert(n.id);
    }

    // connections (simulator format)
    json connections = json::array();
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
            // "lamp1.vout" → "lamp1:vout.ext"
            from_node = from_node + ":" + from_port;
            from_port = "ext";
        }
        if (end_is_bp) {
            // "lamp1.vin" → "lamp1:vin.ext"
            to_node = to_node + ":" + to_port;
            to_port = "ext";
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

    // editor.devices: collapsed Blueprint nodes (visual-only, not in simulator devices)
    json editor_devices = json::array();
    for (const auto& n : bp.nodes) {
        if (n.kind != NodeKind::Blueprint) continue;
        json device = json::object();
        device["name"] = n.id;
        device["kind"] = "Blueprint";
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
            json content = {
                {"type", static_cast<int>(n.node_content.type)},
                {"label", n.node_content.label},
                {"value", n.node_content.value},
                {"min", n.node_content.min},
                {"max", n.node_content.max},
                {"unit", n.node_content.unit}
            };
            if (n.node_content.type == NodeContentType::Switch) {
                content["state"] = n.node_content.state;
            }
            node_state["content"] = content;
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

    // collapsed_groups (editor-only visual hierarchy metadata)
    json collapsed_groups = json::array();
    std::set<std::string> saved_group_ids;  // Dedup collapsed_groups
    for (const auto& group : bp.collapsed_groups) {
        if (!saved_group_ids.insert(group.id).second) continue;
        json group_json = {
            {"id", group.id},
            {"blueprint_path", group.blueprint_path},
            {"type_name", group.type_name},
            {"pos", {{"x", group.pos.x}, {"y", group.pos.y}}},
            {"size", {{"x", group.size.x}, {"y", group.size.y}}},
            {"internal_node_ids", group.internal_node_ids}
        };
        collapsed_groups.push_back(group_json);
    }
    editor["collapsed_groups"] = collapsed_groups;

    j["editor"] = editor;

    return j.dump(2);
}

// ─── Editor save format ────────────────────────────────────────────────────
// Flat list of ALL nodes + wires, no rewriting.
// Hierarchy is metadata only (collapsed_groups), reconstructed on load for visuals.
// blueprint_to_json() is only for simulation (rewrites wires, skips Blueprint kind).

std::string blueprint_to_editor_json(const Blueprint& bp) {
    json j = json::object();

    // Build reverse lookup: node_id → group_id (for tagging devices with group membership)
    std::map<std::string, std::string> node_to_group;
    for (const auto& group : bp.collapsed_groups) {
        for (const auto& nid : group.internal_node_ids)
            node_to_group[nid] = group.id;
    }

    // devices: flat list of ALL nodes including Blueprint kind
    json devices = json::array();
    std::set<std::string> emitted_ids;  // Dedup: skip duplicate node IDs on save
    for (const auto& n : bp.nodes) {
        if (!emitted_ids.insert(n.id).second) continue;
        json device = json::object();
        device["name"] = n.id;
        device["classname"] = n.type_name;
        switch (n.kind) {
            case NodeKind::Bus: device["kind"] = "Bus"; break;
            case NodeKind::Ref: device["kind"] = "Ref"; break;
            case NodeKind::Blueprint: device["kind"] = "Blueprint"; break;
            default: device["kind"] = "Node"; break;
        }

        // group_id: which collapsed group this node belongs to (empty = top-level)
        auto git = node_to_group.find(n.id);
        if (git != node_to_group.end())
            device["group_id"] = git->second;

        // ports — detect InOut by checking if port appears in both inputs and outputs
        std::set<std::string> input_names, output_names;
        for (const auto& p : n.inputs) input_names.insert(p.name);
        for (const auto& p : n.outputs) output_names.insert(p.name);

        json ports = json::object();
        for (const auto& p : n.inputs) {
            bool is_inout = output_names.count(p.name) > 0;
            ports[p.name] = {{"direction", is_inout ? "InOut" : "In"}, {"type", port_type_str(p.type)}};
        }
        for (const auto& p : n.outputs) {
            if (input_names.count(p.name) > 0) continue; // already written as InOut
            ports[p.name] = {{"direction", "Out"}, {"type", port_type_str(p.type)}};
        }
        device["ports"] = ports;

        if (!n.params.empty()) {
            json params = json::object();
            for (const auto& [key, value] : n.params)
                params[key] = value;
            device["params"] = params;
        } else if (n.kind == NodeKind::Ref) {
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
            device["content"] = content;
        }

        devices.push_back(device);
    }
    j["devices"] = devices;

    // wires: flat list, original form, no rewriting
    json wires = json::array();
    for (const auto& w : bp.wires) {
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

    // collapsed_groups: hierarchy metadata for visual grouping
    json collapsed_groups = json::array();
    std::set<std::string> emitted_group_ids;  // Dedup collapsed_groups
    for (const auto& group : bp.collapsed_groups) {
        if (!emitted_group_ids.insert(group.id).second) continue;
        collapsed_groups.push_back({
            {"id", group.id},
            {"blueprint_path", group.blueprint_path},
            {"type_name", group.type_name}
        });
    }
    j["collapsed_groups"] = collapsed_groups;

    // viewport
    j["viewport"] = {
        {"pan", {{"x", bp.pan.x}, {"y", bp.pan.y}}},
        {"zoom", bp.zoom},
        {"grid_step", bp.grid_step}
    };

    return j.dump(2);
}

// Конвертировать device в Node
static Node device_to_node(const json& d, int index) {
    Node n;
    n.id = d.value("name", "");
    n.name = d.value("name", "");
    n.type_name = d.value("classname", "");

    // Определяем вид узла: сначала по явному kind, потом по type_name (backward compat)
    if (d.contains("kind")) {
        std::string kind_str = d["kind"].get<std::string>();
        if (kind_str == "Bus") {
            n.kind = NodeKind::Bus;
        } else if (kind_str == "Ref") {
            n.kind = NodeKind::Ref;
        } else if (kind_str == "Blueprint") {
            n.kind = NodeKind::Blueprint;
        } else {
            n.kind = NodeKind::Node;
        }
    } else if (n.type_name == "Bus") {
        n.kind = NodeKind::Bus;
    } else if (n.type_name == "RefNode" || n.type_name == "Ref") {
        n.kind = NodeKind::Ref;
    } else {
        n.kind = NodeKind::Node;
    }

    // Get size (single source of truth - no registry available here, use defaults)
    n.size = get_default_node_size(n.type_name, nullptr);

    // Позиция - сетка
    int col = index % 4;
    int row = index / 4;
    n.pos = Pt(50 + col * 200, 50 + row * 150);

    // Ports из ports
    if (d.contains("ports") && d["ports"].is_object()) {
        for (auto& [port_name, port_info] : d["ports"].items()) {
            Port p;
            p.name = port_name;
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

    // Read blueprint_path for collapsed blueprint nodes
    if (d.contains("blueprint_path") && n.kind == NodeKind::Blueprint) {
        n.blueprint_path = d["blueprint_path"].get<std::string>();
        // Blueprint nodes are always collapsed when loaded from JSON
        n.collapsed = true;
    }

    // Auto-generate node_content based on device type
    if (n.type_name == "Voltmeter") {
        n.node_content.type = NodeContentType::Gauge;
        n.node_content.label = "";
        n.node_content.value = 0.0f;
        n.node_content.min = 0.0f;
        n.node_content.max = 30.0f;
        n.node_content.unit = "V";
    } else if (n.type_name == "Switch") {
        n.node_content.type = NodeContentType::Switch;
        n.node_content.label = "ON";
        n.node_content.state = false;
    } else if (n.type_name == "HoldButton") {
        n.node_content.type = NodeContentType::Switch;
        n.node_content.label = "RELEASED";
        n.node_content.state = false;
    } else if (n.type_name == "IndicatorLight") {
        n.node_content.type = NodeContentType::Text;
        n.node_content.label = "OFF";
    } else if (n.type_name == "DMR400") {
        n.node_content.type = NodeContentType::Switch;
        n.node_content.label = "ON";
        n.node_content.state = false;
    }

    return n;
}

// Конвертировать DeviceInstance (с портами из ComponentRegistry) в Node
static Node device_instance_to_node(const an24::DeviceInstance& dev, int index = 0,
                                    const an24::ComponentRegistry* registry = nullptr) {
    Node n;
    n.id = dev.name;
    n.name = dev.name;
    n.type_name = dev.classname;

    // Determine node kind
    if (n.type_name == "Bus") {
        n.kind = NodeKind::Bus;
    } else if (n.type_name == "RefNode") {
        n.kind = NodeKind::Ref;
    } else {
        n.kind = NodeKind::Node;
    }

    // Get size from component definition (single source of truth)
    n.size = get_default_node_size(dev.classname, registry);

    // Позиция - сетка (для загрузки из файлов без позиции)
    int col = index % 4;
    int row = index / 4;
    n.pos = Pt(50 + col * 200, 50 + row * 150);

    // Ports из DeviceInstance (уже замержены с ComponentRegistry!)
    for (const auto& [port_name, port] : dev.ports) {
        Port p;
        p.name = port_name;
        p.type = port.type;  // Copy port type for visualization and validation

        if (port.direction == an24::PortDirection::Out) {
            p.side = PortSide::Output;
            n.outputs.push_back(p);
        } else if (port.direction == an24::PortDirection::In) {
            p.side = PortSide::Input;
            n.inputs.push_back(p);
        } else if (port.direction == an24::PortDirection::InOut) {
            // InOut ports go to both inputs and outputs with InOut side
            p.side = PortSide::InOut;
            n.inputs.push_back(p);
            n.outputs.push_back(p);
        }
    }

    // Copy params from DeviceInstance to Node
    n.params = dev.params;

    return n;
}

// Helper: create default node_content based on ComponentDefinition
static NodeContent create_node_content(const an24::ComponentDefinition* def) {
    using namespace an24;

    NodeContent content;
    content.type = NodeContentType::None;

    if (!def) return content;

    // Parse content type from component definition
    std::string content_type_str = def->default_content_type;

    if (content_type_str == "Gauge") {
        content.type = NodeContentType::Gauge;
        content.label = "V";
        content.value = 0.0f;
        content.min = 0.0f;
        content.max = 30.0f;
        content.unit = "V";
    } else if (content_type_str == "Switch") {
        content.type = NodeContentType::Switch;
        content.label = "ON";
        // Read default "closed" state from component definition
        auto it = def->default_params.find("closed");
        if (it != def->default_params.end()) {
            content.state = (it->second == "true");
        } else {
            content.state = false;  // Default to false if not specified
        }
    } else if (content_type_str == "HoldButton") {
        content.type = NodeContentType::Switch;  // Reuse Switch UI (button)
        content.label = "RELEASED";
        content.state = false;  // Default to released state
    } else if (content_type_str == "Text") {
        content.type = NodeContentType::Text;
        content.label = "OFF";
    }

    return content;
}

// Разобрать "device.port" на component и port
static bool parse_port_ref(const std::string& ref, std::string& device, std::string& port) {
    size_t dot = ref.find('.');
    if (dot == std::string::npos) return false;
    device = ref.substr(0, dot);
    port = ref.substr(dot + 1);
    return true;
}

// ─── Auto-layout: compute port center from Node data ───

static constexpr float LAYOUT_GRID = 16.0f;

static Pt snap(Pt p) {
    return Pt(std::round(p.x / LAYOUT_GRID) * LAYOUT_GRID,
              std::round(p.y / LAYOUT_GRID) * LAYOUT_GRID);
}

/// Compute world position of a port on a node (without VisualNode objects).
/// Inputs on left edge, Outputs on right edge, Bus center-bottom, Ref center-top.
static Pt port_center(const Node& n, const std::string& port_name) {
    if (n.kind == NodeKind::Ref) {
        return snap(Pt(n.pos.x + n.size.x / 2, n.pos.y));  // top center
    }
    if (n.kind == NodeKind::Bus) {
        return snap(Pt(n.pos.x + n.size.x / 2, n.pos.y + n.size.y));  // bottom center
    }
    // Standard node: inputs on left, outputs on right
    for (size_t i = 0; i < n.inputs.size(); i++) {
        if (n.inputs[i].name == port_name) {
            float step = n.size.y / (float)(n.inputs.size() + 1);
            return snap(Pt(n.pos.x, n.pos.y + step * (i + 1)));
        }
    }
    for (size_t i = 0; i < n.outputs.size(); i++) {
        if (n.outputs[i].name == port_name) {
            float step = n.size.y / (float)(n.outputs.size() + 1);
            return snap(Pt(n.pos.x + n.size.x, n.pos.y + step * (i + 1)));
        }
    }
    return snap(Pt(n.pos.x + n.size.x / 2, n.pos.y + n.size.y / 2));
}

// ─── Auto-layout: assign node positions by circuit topology ───

enum class NodeRole { Source, Bus, Load, Ground };

static NodeRole classify_node(const Node& n) {
    if (n.type_name == "RefNode" || n.type_name == "Ref") return NodeRole::Ground;
    if (n.type_name == "Bus") return NodeRole::Bus;
    if (n.type_name == "Battery" || n.type_name == "Generator")
        return NodeRole::Source;
    return NodeRole::Load;
}

/// Auto-layout the blueprint nodes by topology and auto-route wires.
static void auto_layout(Blueprint& bp) {
    if (bp.nodes.empty()) return;

    // Classify nodes into layers
    std::vector<size_t> sources, buses, loads, grounds;
    for (size_t i = 0; i < bp.nodes.size(); i++) {
        switch (classify_node(bp.nodes[i])) {
        case NodeRole::Ground: grounds.push_back(i); break;
        case NodeRole::Source: sources.push_back(i); break;
        case NodeRole::Bus:    buses.push_back(i); break;
        case NodeRole::Load:   loads.push_back(i); break;
        }
    }

    // Build adjacency: for each bus, find connected sources/loads to order them
    std::map<std::string, std::set<std::string>> bus_connections;
    for (const auto& w : bp.wires) {
        bus_connections[w.start.node_id].insert(w.end.node_id);
        bus_connections[w.end.node_id].insert(w.start.node_id);
    }

    // Layout constants
    constexpr float col_spacing = 240.0f;
    constexpr float row_spacing = 160.0f;
    constexpr float origin_x = 80.0f;
    constexpr float origin_y = 80.0f;

    // Column X positions: sources | buses | loads
    float src_x = origin_x;
    float bus_x = origin_x + col_spacing;
    float load_x = origin_x + col_spacing * 2;

    // Place sources vertically
    for (size_t i = 0; i < sources.size(); i++) {
        auto& n = bp.nodes[sources[i]];
        n.pos = snap(Pt(src_x, origin_y + i * row_spacing));
    }

    // Place buses vertically, sizing based on connection count
    for (size_t i = 0; i < buses.size(); i++) {
        auto& n = bp.nodes[buses[i]];
        size_t conn_count = bus_connections[n.id].size();
        float bus_w = std::max(3.0f, (float)(conn_count + 2)) * LAYOUT_GRID;
        n.size = snap(Pt(bus_w, LAYOUT_GRID * 2));
        n.pos = snap(Pt(bus_x, origin_y + i * row_spacing));
    }

    // Place loads vertically
    for (size_t i = 0; i < loads.size(); i++) {
        auto& n = bp.nodes[loads[i]];
        n.pos = snap(Pt(load_x, origin_y + i * row_spacing));
    }

    // Place grounds below sources, centered under bus column
    float ground_y = origin_y + std::max({sources.size(), buses.size(), loads.size()}) * row_spacing;
    for (size_t i = 0; i < grounds.size(); i++) {
        auto& n = bp.nodes[grounds[i]];
        n.pos = snap(Pt(bus_x, ground_y + i * row_spacing));
    }

    // Auto-route wires using A* router
    std::map<std::string, size_t> id_to_index;
    for (size_t i = 0; i < bp.nodes.size(); i++)
        id_to_index[bp.nodes[i].id] = i;

    std::vector<std::vector<Pt>> existing_paths;
    for (auto& w : bp.wires) {
        auto it_s = id_to_index.find(w.start.node_id);
        auto it_e = id_to_index.find(w.end.node_id);
        if (it_s == id_to_index.end() || it_e == id_to_index.end()) continue;

        const Node& sn = bp.nodes[it_s->second];
        const Node& en = bp.nodes[it_e->second];
        Pt start = port_center(sn, w.start.port_name);
        Pt end   = port_center(en, w.end.port_name);

        auto path = route_around_nodes(
            start, end, sn, w.start.port_name.c_str(),
            en, w.end.port_name.c_str(),
            bp.nodes, LAYOUT_GRID, existing_paths);

        if (!path.empty()) {
            // Store routing points (skip first and last — they're port positions)
            w.routing_points.clear();
            for (size_t i = 1; i + 1 < path.size(); i++)
                w.routing_points.push_back(path[i]);
            existing_paths.push_back(std::move(path));
        }
    }
}

// ─── Apply port types from component registry ──────────────────────────────

static void apply_port_types_from_registry(Node& n, const an24::ComponentRegistry& registry) {
    const auto* def = registry.get(n.type_name);
    if (!def) return;
    for (auto& p : n.inputs) {
        auto it = def->default_ports.find(p.name);
        if (it != def->default_ports.end()) p.type = it->second.type;
    }
    for (auto& p : n.outputs) {
        auto it = def->default_ports.find(p.name);
        if (it != def->default_ports.end()) p.type = it->second.type;
    }
}

// ─── Load: new editor format (flat devices + wires + collapsed_groups) ─────

static std::optional<Blueprint> load_editor_format(const json& j) {
    Blueprint bp;

    // Load devices → nodes (all inline: pos, size, kind, content, group_id)
    std::set<std::string> loaded_ids;  // Dedup: skip duplicate node IDs
    for (const auto& d : j["devices"]) {
        Node n;
        n.id = d.value("name", "");
        n.name = n.id;
        n.type_name = d.value("classname", "");

        // Skip duplicate node IDs (malformed saves may contain duplicates)
        if (!loaded_ids.insert(n.id).second) continue;

        // Kind
        std::string kind_str = d.value("kind", "Node");
        if (kind_str == "Bus") n.kind = NodeKind::Bus;
        else if (kind_str == "Ref") n.kind = NodeKind::Ref;
        else if (kind_str == "Blueprint") n.kind = NodeKind::Blueprint;
        else n.kind = NodeKind::Node;

        // Ports
        if (d.contains("ports") && d["ports"].is_object()) {
            for (auto& [port_name, port_info] : d["ports"].items()) {
                Port p;
                p.name = port_name;
                if (port_info.contains("type")) {
                    p.type = parse_port_type_str(port_info["type"].get<std::string>());
                } else {
                    spdlog::error("Port '{}' on device '{}' missing 'type' field in JSON", port_name, n.id);
                    std::abort();
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
            if (n.kind == NodeKind::Blueprint)
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

        bp.nodes.push_back(std::move(n));
    }

    // Resolve port types from component registry
    an24::ComponentRegistry registry = an24::load_component_registry();
    for (auto& n : bp.nodes)
        apply_port_types_from_registry(n, registry);

    // Load wires (flat, original form)
    for (const auto& ws : j["wires"]) {
        std::string from = ws.value("from", "");
        std::string to = ws.value("to", "");

        std::string from_device, from_port, to_device, to_port;
        if (!parse_port_ref(from, from_device, from_port) ||
            !parse_port_ref(to, to_device, to_port))
            continue;

        Wire w;
        w.id = from + "→" + to;
        w.start.node_id = from_device;
        w.start.port_name = from_port;
        w.end.node_id = to_device;
        w.end.port_name = to_port;

        if (ws.contains("routing_points") && ws["routing_points"].is_array()) {
            for (const auto& pj : ws["routing_points"]) {
                w.routing_points.push_back(Pt(pj.value("x", 0.0f), pj.value("y", 0.0f)));
            }
        }

        bp.wires.push_back(std::move(w));
    }

    // Reconstruct collapsed_groups from group metadata + group_id on devices
    if (j.contains("collapsed_groups") && j["collapsed_groups"].is_array()) {
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

        std::set<std::string> loaded_group_ids;  // Dedup collapsed_groups by ID
        for (const auto& gj : j["collapsed_groups"]) {
            CollapsedGroup group;
            group.id = gj.value("id", "");
            if (!loaded_group_ids.insert(group.id).second) continue;
            group.blueprint_path = gj.value("blueprint_path", "");
            group.type_name = gj.value("type_name", "");

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

            // Get pos/size from the Blueprint kind node (single source of truth)
            for (const auto& n : bp.nodes) {
                if (n.id == group.id && n.kind == NodeKind::Blueprint) {
                    group.pos = n.pos;
                    group.size = n.size;
                    break;
                }
            }

            bp.collapsed_groups.push_back(group);
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

    bp.recompute_visibility();
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
