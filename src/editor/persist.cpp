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
#include <set>
#include <queue>

using json = nlohmann::json;

// Публичные функции

std::string blueprint_to_json(const Blueprint& bp) {
    json j = json::object();

    // Unified format: devices + connections (simulator format)
    // + editor section for visual debugging metadata

    // templates (empty for now)
    j["templates"] = json::object();

    // devices (simulator format) - full format for AOT/JIT compatibility
    json devices = json::array();
    for (const auto& n : bp.nodes) {
        json device = json::object();
        device["name"] = n.id;
        device["template_name"] = "";
        device["classname"] = n.type_name;
        // Store NodeKind explicitly so it roundtrips correctly
        switch (n.kind) {
            case NodeKind::Bus: device["kind"] = "Bus"; break;
            case NodeKind::Ref: device["kind"] = "Ref"; break;
            default: device["kind"] = "Node"; break;
        }
        device["priority"] = "med";
        device["bucket"] = nullptr;
        device["critical"] = false;
        device["is_composite"] = false;
        // ports
        json ports = json::object();
        for (const auto& p : n.inputs) {
            ports[p.name] = {{"direction", "In"}};
        }
        for (const auto& p : n.outputs) {
            ports[p.name] = {{"direction", "Out"}};
        }
        device["ports"] = ports;
        // NOTE: UI params (label, value, min, max, unit) are stored in editor.nodes, NOT in device.params
        // NOTE: Domains are NOT saved to JSON - they are defined in component definitions
        devices.push_back(device);
    }
    j["devices"] = devices;

    // connections (simulator format)
    json connections = json::array();
    for (const auto& w : bp.wires) {
        json conn = json::object();
        conn["from"] = w.start.node_id + "." + w.start.port_name;
        conn["to"] = w.end.node_id + "." + w.end.port_name;
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

    j["editor"] = editor;

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

std::optional<Blueprint> blueprint_from_json(const std::string& json_str) {
    try {
        // Keep raw JSON for validation
        json j = json::parse(json_str);

        // Validate that devices array exists
        if (!j.contains("devices") || !j["devices"].is_array()) {
            return std::nullopt;  // Invalid format - must have devices
        }

        // Parse JSON with ComponentRegistry merge (this adds ports from component definitions!)
        // Same approach as in simulation.cpp
        an24::ParserContext ctx = an24::parse_json(json_str);

        Blueprint bp;
        std::map<std::string, size_t> device_indices;

        // Convert devices to nodes (using merged DeviceInstance with ports from ComponentRegistry)
        int idx = 0;
        size_t dev_idx = 0;
        for (const auto& dev : ctx.devices) {
            Node n = device_instance_to_node(dev, idx, &ctx.registry);

            // Restore kind from JSON if explicitly set (overrides type_name-based detection)
            if (j["devices"][dev_idx].contains("kind")) {
                std::string kind_str = j["devices"][dev_idx]["kind"].get<std::string>();
                if (kind_str == "Bus") {
                    n.kind = NodeKind::Bus;
                } else if (kind_str == "Ref") {
                    n.kind = NodeKind::Ref;
                } else if (kind_str == "Node") {
                    n.kind = NodeKind::Node;
                }
                // Re-calculate size based on kind (single source of truth)
                n.size = get_default_node_size(n.type_name, &ctx.registry);
            }

            // Create node_content from ComponentDefinition
            const auto* def = ctx.registry.get(dev.classname);
            if (def) {
                n.node_content = create_node_content(def);
            }

            device_indices[n.id] = bp.nodes.size();
            bp.nodes.push_back(n);
            idx++;
            dev_idx++;
        }

        // Convert connections to wires
        if (j.contains("connections") && j["connections"].is_array()) {
            for (const auto& conn : j["connections"]) {
                std::string from = conn.value("from", "");
                std::string to = conn.value("to", "");

                std::string from_device, from_port;
                std::string to_device, to_port;

                if (parse_port_ref(from, from_device, from_port) &&
                    parse_port_ref(to, to_device, to_port)) {

                    auto it_from = device_indices.find(from_device);
                    auto it_to = device_indices.find(to_device);

                    if (it_from != device_indices.end() && it_to != device_indices.end()) {
                        Wire w;
                        w.id = from + "→" + to;
                        w.start.node_id = from_device;
                        w.start.port_name = from_port;
                        w.end.node_id = to_device;
                        w.end.port_name = to_port;
                        bp.wires.push_back(w);
                    }
                }
            }
        }

        // Apply editor metadata (visual debugging state)
        if (j.contains("editor")) {
            const auto& editor = j["editor"];

            // viewport
            if (editor.contains("viewport")) {
                const auto& vp = editor["viewport"];
                if (vp.contains("pan")) {
                    bp.pan.x = vp["pan"].value("x", 0.0f);
                    bp.pan.y = vp["pan"].value("y", 0.0f);
                }
                bp.zoom = vp.value("zoom", 1.0f);
                bp.grid_step = vp.value("grid_step", 16.0f);
            }

            // node visual states (position + size + content)
            if (editor.contains("nodes") && editor["nodes"].is_object()) {
                for (auto& [node_id, node_state] : editor["nodes"].items()) {
                    for (auto& n : bp.nodes) {
                        if (n.id == node_id) {
                            if (node_state.contains("pos")) {
                                n.pos.x = node_state["pos"].value("x", n.pos.x);
                                n.pos.y = node_state["pos"].value("y", n.pos.y);
                            }
                            if (node_state.contains("size")) {
                                n.size.x = node_state["size"].value("x", n.size.x);
                                n.size.y = node_state["size"].value("y", n.size.y);
                            }
                            // Restore node_content (UI metadata)
                            if (node_state.contains("content")) {
                                const auto& content = node_state["content"];
                                if (content.contains("type"))
                                    n.node_content.type = static_cast<NodeContentType>(content["type"].get<int>());
                                if (content.contains("label"))
                                    n.node_content.label = content["label"].get<std::string>();
                                if (content.contains("value"))
                                    n.node_content.value = content["value"].get<float>();
                                if (content.contains("min"))
                                    n.node_content.min = content["min"].get<float>();
                                if (content.contains("max"))
                                    n.node_content.max = content["max"].get<float>();
                                if (content.contains("unit"))
                                    n.node_content.unit = content["unit"].get<std::string>();
                                if (content.contains("state"))
                                    n.node_content.state = content["state"].get<bool>();
                            }
                            break;
                        }
                    }
                }
            }

            // wire routing points
            if (editor.contains("wires") && editor["wires"].is_array()) {
                for (const auto& ws : editor["wires"]) {
                    std::string from = ws.value("from", "");
                    std::string to = ws.value("to", "");

                    for (auto& w : bp.wires) {
                        std::string w_from = w.start.node_id + "." + w.start.port_name;
                        std::string w_to = w.end.node_id + "." + w.end.port_name;
                        if (w_from == from && w_to == to) {
                            if (ws.contains("routing_points") && ws["routing_points"].is_array()) {
                                w.routing_points.clear();
                                for (const auto& pj : ws["routing_points"]) {
                                    Pt pt;
                                    pt.x = pj.value("x", 0.0f);
                                    pt.y = pj.value("y", 0.0f);
                                    w.routing_points.push_back(pt);
                                }
                            }
                            break;
                        }
                    }
                }
            }
        } else {
            // No editor metadata — auto-layout nodes and route wires
            auto_layout(bp);
        }

        // [f6g7h8i9] Initialize next_wire_id to be higher than any existing wire_N IDs
        bp.next_wire_id = static_cast<int>(bp.wires.size());
        for (const auto& w : bp.wires) {
            if (w.id.compare(0, 5, "wire_") == 0) {
                int num = std::atoi(w.id.c_str() + 5);
                if (num >= bp.next_wire_id) bp.next_wire_id = num + 1;
            }
        }

        return bp;
    } catch (const std::exception& e) {
        (void)e;
        return std::nullopt;
    }
}

bool save_blueprint_to_file(const Blueprint& bp, const char* path) {
    std::string json_str = blueprint_to_json(bp);
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
