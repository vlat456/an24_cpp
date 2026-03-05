#include "persist.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <optional>

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
        device["internal"] = n.type_name;
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
        // params from content
        json params = json::object();
        if (n.node_content.type != NodeContentType::None) {
            if (!n.node_content.label.empty()) params["label"] = n.node_content.label;
            params["value"] = std::to_string(n.node_content.value);
            params["min"] = std::to_string(n.node_content.min);
            params["max"] = std::to_string(n.node_content.max);
            if (!n.node_content.unit.empty()) params["unit"] = n.node_content.unit;
        }
        if (!params.empty()) {
            device["params"] = params;
        }
        // default domain
        device["explicit_domains"] = {"Electrical"};
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

    // node visual states (position + size per device)
    json node_states = json::object();
    for (const auto& n : bp.nodes) {
        node_states[n.id] = {
            {"pos", {{"x", n.pos.x}, {"y", n.pos.y}}},
            {"size", {{"x", n.size.x}, {"y", n.size.y}}}
        };
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
    n.type_name = d.value("internal", "");

    // Определяем вид узла по типу
    if (n.type_name == "Bus") {
        n.kind = NodeKind::Bus;
        n.size = Pt(40, 40); // маленький квадрат
    } else if (n.type_name == "RefNode" || n.type_name == "Ref") {
        n.kind = NodeKind::Ref;
        n.size = Pt(40, 40);
    } else {
        n.kind = NodeKind::Node;
    }

    // Размер по умолчанию
    n.size = Pt(120, 80);

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

    return n;
}

// Разобрать "device.port" на component и port
static bool parse_port_ref(const std::string& ref, std::string& device, std::string& port) {
    size_t dot = ref.find('.');
    if (dot == std::string::npos) return false;
    device = ref.substr(0, dot);
    port = ref.substr(dot + 1);
    return true;
}

std::optional<Blueprint> blueprint_from_json(const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        Blueprint bp;

        // Unified format: devices + connections + editor (visual debugging metadata)
        // Both simulator and editor use same format

        if (!j.contains("devices") || !j["devices"].is_array()) {
            return std::nullopt;  // Invalid format - must have devices
        }

        std::map<std::string, size_t> device_indices;

        // Convert devices to nodes
        int idx = 0;
        for (const auto& dev : j["devices"]) {
            Node n = device_to_node(dev, idx);
            device_indices[n.id] = bp.nodes.size();
            bp.nodes.push_back(n);
            idx++;
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

            // node visual states (position + size)
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
