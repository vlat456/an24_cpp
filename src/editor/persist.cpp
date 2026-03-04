#include "persist.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <optional>

using json = nlohmann::json;

// Публичные функции

std::string blueprint_to_json(const Blueprint& bp) {
    json j = json::object();

    // nodes
    json nodes = json::array();
    for (const auto& n : bp.nodes) {
        json node = json::object();
        node["id"] = n.id;
        node["name"] = n.name;
        node["type_name"] = n.type_name;
        node["pos"] = {{"x", n.pos.x}, {"y", n.pos.y}};
        node["size"] = {{"x", n.size.x}, {"y", n.size.y}};

        // inputs
        json inputs = json::array();
        for (const auto& p : n.inputs) {
            inputs.push_back({{"name", p.name}, {"side", p.side == PortSide::Input ? "input" : "output"}});
        }
        node["inputs"] = inputs;

        // outputs
        json outputs = json::array();
        for (const auto& p : n.outputs) {
            outputs.push_back({{"name", p.name}, {"side", p.side == PortSide::Input ? "input" : "output"}});
        }
        node["outputs"] = outputs;

        // content
        json content = json::object();
        content["type"] = static_cast<int>(n.node_content.type);
        content["label"] = n.node_content.label;
        content["value"] = n.node_content.value;
        content["min"] = n.node_content.min;
        content["max"] = n.node_content.max;
        content["unit"] = n.node_content.unit;
        content["state"] = n.node_content.state;
        node["content"] = content;

        nodes.push_back(node);
    }
    j["nodes"] = nodes;

    // wires
    json wires = json::array();
    for (const auto& w : bp.wires) {
        json wire = json::object();
        wire["id"] = w.id;
        wire["start"] = {{"node_id", w.start.node_id}, {"port_name", w.start.port_name}};
        wire["end"] = {{"node_id", w.end.node_id}, {"port_name", w.end.port_name}};

        // routing points
        json rps = json::array();
        for (const auto& pt : w.routing_points) {
            rps.push_back({{"x", pt.x}, {"y", pt.y}});
        }
        wire["routing_points"] = rps;

        wires.push_back(wire);
    }
    j["wires"] = wires;

    // viewport
    j["pan"] = {{"x", bp.pan.x}, {"y", bp.pan.y}};
    j["zoom"] = bp.zoom;
    j["grid_step"] = bp.grid_step;

    return j.dump(2);
}

// Конвертировать device в Node
static Node device_to_node(const json& d, int index) {
    Node n;
    n.id = d.value("name", "");
    n.name = d.value("name", "");
    n.type_name = d.value("internal", "");

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

        // Проверяем формат: если есть "devices" - это формат симулятора
        if (j.contains("devices") && j["devices"].is_array()) {
            // Формат симулятора: devices + connections
            std::map<std::string, size_t> device_indices;

            // Конвертируем devices в nodes
            int idx = 0;
            for (const auto& dev : j["devices"]) {
                Node n = device_to_node(dev, idx);
                device_indices[n.id] = bp.nodes.size();
                bp.nodes.push_back(n);
                idx++;
            }

            // Конвертируем connections в wires
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

            return bp;
        }

        // Иначе - нативный формат Blueprint (nodes + wires)
        if (j.contains("nodes") && j["nodes"].is_array()) {
            for (const auto& nj : j["nodes"]) {
                Node n;
                n.id = nj.value("id", "");
                n.name = nj.value("name", "");
                n.type_name = nj.value("type_name", "");

                if (nj.contains("pos")) {
                    n.pos.x = nj["pos"].value("x", 0.0f);
                    n.pos.y = nj["pos"].value("y", 0.0f);
                }
                if (nj.contains("size")) {
                    n.size.x = nj["size"].value("x", 120.0f);
                    n.size.y = nj["size"].value("y", 80.0f);
                }

                // inputs
                if (nj.contains("inputs") && nj["inputs"].is_array()) {
                    for (const auto& pj : nj["inputs"]) {
                        Port p;
                        p.name = pj.value("name", "");
                        p.side = (pj.value("side", "") == "output") ? PortSide::Output : PortSide::Input;
                        n.inputs.push_back(p);
                    }
                }

                // outputs
                if (nj.contains("outputs") && nj["outputs"].is_array()) {
                    for (const auto& pj : nj["outputs"]) {
                        Port p;
                        p.name = pj.value("name", "");
                        p.side = (pj.value("side", "") == "output") ? PortSide::Output : PortSide::Input;
                        n.outputs.push_back(p);
                    }
                }

                // content
                if (nj.contains("content")) {
                    const auto& cj = nj["content"];
                    n.node_content.type = static_cast<NodeContentType>(cj.value("type", 0));
                    n.node_content.label = cj.value("label", "");
                    n.node_content.value = cj.value("value", 0.0f);
                    n.node_content.min = cj.value("min", 0.0f);
                    n.node_content.max = cj.value("max", 1.0f);
                    n.node_content.unit = cj.value("unit", "");
                    n.node_content.state = cj.value("state", false);
                }

                bp.nodes.push_back(n);
            }
        }

        // wires
        if (j.contains("wires") && j["wires"].is_array()) {
            for (const auto& wj : j["wires"]) {
                Wire w;
                w.id = wj.value("id", "");

                if (wj.contains("start")) {
                    w.start.node_id = wj["start"].value("node_id", "");
                    w.start.port_name = wj["start"].value("port_name", "");
                }
                if (wj.contains("end")) {
                    w.end.node_id = wj["end"].value("node_id", "");
                    w.end.port_name = wj["end"].value("port_name", "");
                }

                // routing points
                if (wj.contains("routing_points") && wj["routing_points"].is_array()) {
                    for (const auto& pj : wj["routing_points"]) {
                        Pt pt;
                        pt.x = pj.value("x", 0.0f);
                        pt.y = pj.value("y", 0.0f);
                        w.routing_points.push_back(pt);
                    }
                }

                bp.wires.push_back(w);
            }
        }

        // viewport
        if (j.contains("pan")) {
            bp.pan.x = j["pan"].value("x", 0.0f);
            bp.pan.y = j["pan"].value("y", 0.0f);
        }
        bp.zoom = j.value("zoom", 1.0f);
        bp.grid_step = j.value("grid_step", 16.0f);

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
