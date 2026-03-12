#include "blueprint.h"
#include "port.h"
#include "layout_constants.h"
#include "flat_blueprint.h"
#include <set>
#include <map>
#include <algorithm>
#include <cstdio>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// BUGFIX [e4a1b7] Runtime dedup: reject exact-duplicate wires with warning
// [2.1] O(1) set-based dedup replaces O(n) linear scan
size_t Blueprint::add_wire(Wire wire) {
    WireKey key(wire);
    if (!wire_index_.insert(key).second) {
        spdlog::warn("[dedup] Runtime duplicate wire rejected: {}.{} → {}.{}",
            wire.start.node_id, wire.start.port_name,
            wire.end.node_id, wire.end.port_name);
        return SIZE_MAX;
    }
    size_t idx = wires.size();
    wires.push_back(std::move(wire));
    return idx;
}

/// Check if two port types are compatible for connection
static bool are_ports_compatible(PortType from_type, PortType to_type) {
    // Any type is wildcard - compatible with everything
    if (from_type == PortType::Any || to_type == PortType::Any) {
        return true;
    }
    // Types must match exactly
    return from_type == to_type;
}

/// Find a port in a node's inputs or outputs
static PortType find_port_type(const Node& node, const std::string& port_name) {
    // Search inputs
    for (const auto& port : node.inputs) {
        if (port.name == port_name) {
            return port.type;
        }
    }
    // Search outputs
    for (const auto& port : node.outputs) {
        if (port.name == port_name) {
            return port.type;
        }
    }
    // Default to Any if not found
    return PortType::Any;
}

// [SMELL-k1l2] Removed 17 fprintf debug lines — use spdlog for diagnostic logging instead
bool Blueprint::add_wire_validated(Wire wire) {
    // Find the start and end nodes
    Node* start_node = nullptr;
    Node* end_node = nullptr;

    for (auto& n : nodes) {
        if (n.id == wire.start.node_id) start_node = &n;
        if (n.id == wire.end.node_id) end_node = &n;
    }

    if (!start_node || !end_node) return false;

    // Get port types
    PortType start_type = find_port_type(*start_node, wire.start.port_name);
    PortType end_type = find_port_type(*end_node, wire.end.port_name);

    // Check compatibility
    if (!are_ports_compatible(start_type, end_type)) return false;

    // Check one-to-one connections (except for Bus/RefNode)
    bool start_allows_multiple = (start_node->type_name == "Bus" ||
                                   start_node->type_name == "RefNode");
    bool end_allows_multiple = (end_node->type_name == "Bus" ||
                                 end_node->type_name == "RefNode");

    // Check if start port is already occupied
    if (!start_allows_multiple) {
        for (const auto& w : wires) {
            if ((w.start.node_id == wire.start.node_id && w.start.port_name == wire.start.port_name) ||
                (w.end.node_id == wire.start.node_id && w.end.port_name == wire.start.port_name))
                return false;
        }
    }

    // Check if end port is already occupied
    if (!end_allows_multiple) {
        for (const auto& w : wires) {
            if ((w.end.node_id == wire.end.node_id && w.end.port_name == wire.end.port_name) ||
                (w.start.node_id == wire.end.node_id && w.start.port_name == wire.end.port_name))
                return false;
        }
    }

    wires.push_back(std::move(wire));
    wire_index_.insert(WireKey(wires.back()));
    return true;
}

// ─── SubBlueprintInstance helpers ───

SubBlueprintInstance* Blueprint::find_sub_blueprint_instance(const std::string& id) {
    for (auto& sbi : sub_blueprint_instances) {
        if (sbi.id == id) return &sbi;
    }
    return nullptr;
}

const SubBlueprintInstance* Blueprint::find_sub_blueprint_instance(const std::string& id) const {
    for (const auto& sbi : sub_blueprint_instances) {
        if (sbi.id == id) return &sbi;
    }
    return nullptr;
}

bool Blueprint::remove_sub_blueprint_instance(const std::string& id) {
    auto it = std::find_if(sub_blueprint_instances.begin(), sub_blueprint_instances.end(),
                           [&](const SubBlueprintInstance& s) { return s.id == id; });
    if (it == sub_blueprint_instances.end()) return false;
    sub_blueprint_instances.erase(it);
    return true;
}

bool Blueprint::bake_in_sub_blueprint(const std::string& id) {
    auto sbi_it = std::find_if(sub_blueprint_instances.begin(), sub_blueprint_instances.end(),
                               [&](const SubBlueprintInstance& s) { return s.id == id; });
    if (sbi_it == sub_blueprint_instances.end()) return false;

    SubBlueprintInstance& sbi = *sbi_it;
    if (sbi.baked_in) return false;

    std::string prefix = sbi.id + ":";

    for (const auto& [override_key, override_val] : sbi.params_override) {
        auto dot = override_key.find('.');
        if (dot == std::string::npos) continue;
        std::string dev_name = override_key.substr(0, dot);
        std::string param_name = override_key.substr(dot + 1);
        std::string target_id = sbi.id + ":" + dev_name;

        Node* node = find_node(target_id.c_str());
        if (node) {
            node->params[param_name] = override_val;
        }
    }

    for (const auto& [dev_name, pos] : sbi.layout_override) {
        std::string target_id = sbi.id + ":" + dev_name;
        Node* node = find_node(target_id.c_str());
        if (node) {
            node->pos = pos;
        }
    }

    for (auto& wire : wires) {
        if (wire.start.node_id.size() <= prefix.size() || 
            wire.start.node_id.substr(0, prefix.size()) != prefix) {
            continue;
        }
        if (wire.end.node_id.size() <= prefix.size() || 
            wire.end.node_id.substr(0, prefix.size()) != prefix) {
            continue;
        }
        std::string start_unprefixed = wire.start.node_id.substr(prefix.size());
        std::string end_unprefixed = wire.end.node_id.substr(prefix.size());
        std::string wk = start_unprefixed + "." +
                         wire.start.port_name + "->" +
                         end_unprefixed + "." +
                         wire.end.port_name;
        auto rt_it = sbi.internal_routing.find(wk);
        if (rt_it != sbi.internal_routing.end()) {
            wire.routing_points = rt_it->second;
        }
    }

    std::vector<std::string> internal_ids;
    for (const auto& n : nodes) {
        if (n.group_id == sbi.id && n.id != sbi.id) {
            internal_ids.push_back(n.id);
        }
    }

    sbi_it->baked_in = true;
    sbi_it->internal_node_ids = internal_ids;
    sbi_it->params_override.clear();
    sbi_it->layout_override.clear();
    sbi_it->internal_routing.clear();

    return true;
}

// ─── Auto-layout for a single group ───

static Pt layout_snap(Pt p) {
    return Pt(std::round(p.x / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID,
              std::round(p.y / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID);
}

void Blueprint::auto_layout_group(const std::string& group_id) {
    // BUGFIX [a2d7c5] BlueprintInput → leftmost column, BlueprintOutput → rightmost
    // Collect indices of nodes in this group by role
    std::vector<size_t> bp_inputs, sources, buses, loads, grounds, bp_outputs;
    for (size_t i = 0; i < nodes.size(); i++) {
        if (nodes[i].group_id != group_id) continue;
        const auto& tn = nodes[i].type_name;
        if (tn == "BlueprintInput")
            bp_inputs.push_back(i);
        else if (tn == "BlueprintOutput")
            bp_outputs.push_back(i);
        else if (nodes[i].render_hint == "ref")
            grounds.push_back(i);
        else if (nodes[i].render_hint == "bus" || tn == "Bus")
            buses.push_back(i);
        else if (tn == "Battery" || tn == "Generator")
            sources.push_back(i);
        else
            loads.push_back(i);
    }

    if (bp_inputs.empty() && sources.empty() && buses.empty() &&
        loads.empty() && grounds.empty() && bp_outputs.empty())
        return;

    constexpr float col_spacing = editor_constants::LAYOUT_COL_SPACING;
    constexpr float row_spacing = editor_constants::LAYOUT_ROW_SPACING;
    constexpr float origin_x = editor_constants::LAYOUT_ORIGIN_X;
    constexpr float origin_y = editor_constants::LAYOUT_ORIGIN_Y;

    // Columns left→right: BlueprintInput | Sources | Buses | Loads | BlueprintOutput
    float col = origin_x;
    float bpi_x = col;   if (!bp_inputs.empty())  col += col_spacing;
    float src_x = col;   if (!sources.empty())    col += col_spacing;
    float bus_x = col;   if (!buses.empty())      col += col_spacing;
    float load_x = col;  if (!loads.empty())      col += col_spacing;
    float bpo_x = col;

    for (size_t i = 0; i < bp_inputs.size(); i++)
        nodes[bp_inputs[i]].pos = layout_snap(Pt(bpi_x, origin_y + i * row_spacing));
    for (size_t i = 0; i < sources.size(); i++)
        nodes[sources[i]].pos = layout_snap(Pt(src_x, origin_y + i * row_spacing));
    for (size_t i = 0; i < buses.size(); i++)
        nodes[buses[i]].pos = layout_snap(Pt(bus_x, origin_y + i * row_spacing));
    for (size_t i = 0; i < loads.size(); i++)
        nodes[loads[i]].pos = layout_snap(Pt(load_x, origin_y + i * row_spacing));
    for (size_t i = 0; i < bp_outputs.size(); i++)
        nodes[bp_outputs[i]].pos = layout_snap(Pt(bpo_x, origin_y + i * row_spacing));

    size_t max_rows = std::max({bp_inputs.size(), sources.size(), buses.size(),
                                loads.size(), bp_outputs.size(), size_t(1)});
    float ground_y = origin_y + max_rows * row_spacing;
    for (size_t i = 0; i < grounds.size(); i++)
        nodes[grounds[i]].pos = layout_snap(Pt(bus_x, ground_y + i * row_spacing));
}

// ─── Shared blueprint expansion ───

Blueprint expand_type_definition(const TypeDefinition& def, const TypeRegistry& registry) {
     Blueprint bp;

    for (const auto& dev : def.devices) {
        Node node;
        node.id = dev.name;
        node.name = dev.name;
        node.type_name = dev.classname;

        // Position and size from stored layout (editor-saved blueprints)
        if (dev.pos)
            node.pos = Pt(dev.pos->first, dev.pos->second);
        if (dev.size)
            node.size = Pt(dev.size->first, dev.size->second);
        else
            node.size = get_default_node_size(dev.classname, &registry);

        // Type info from registry
        const auto* inner_def = registry.get(dev.classname);
        if (inner_def) {
            node.render_hint = inner_def->render_hint;
            node.expandable = !inner_def->cpp_class && !inner_def->devices.empty();
            node.node_content = create_node_content_from_def(inner_def);
        }

        // Ports: prefer inline ports from device, fall back to registry
        const auto& port_source = !dev.ports.empty() ? dev.ports
                                : (inner_def ? inner_def->ports : dev.ports);
        for (const auto& [port_name, port] : port_source) {
            if (port.direction == PortDirection::In || port.direction == PortDirection::InOut)
                node.inputs.emplace_back(port_name.c_str(), PortSide::Input, port.type);
            if (port.direction == PortDirection::Out || port.direction == PortDirection::InOut)
                node.outputs.emplace_back(port_name.c_str(), PortSide::Output, port.type);
        }

        // Params: registry defaults + device overrides
        if (inner_def)
            node.params = inner_def->params;
        for (const auto& [k, v] : dev.params)
            node.params[k] = v;

        bp.add_node(std::move(node));
    }

    for (const auto& conn : def.connections) {
        Wire wire;
        wire.id = conn.from + "->" + conn.to;

        size_t dot = conn.from.find('.');
        if (dot != std::string::npos) {
            wire.start.node_id = conn.from.substr(0, dot);
            wire.start.port_name = conn.from.substr(dot + 1);
        }
        dot = conn.to.find('.');
        if (dot != std::string::npos) {
            wire.end.node_id = conn.to.substr(0, dot);
            wire.end.port_name = conn.to.substr(dot + 1);
        }

        for (const auto& [x, y] : conn.routing_points)
            wire.routing_points.push_back(Pt(x, y));

        bp.add_wire(std::move(wire));
    }

    return bp;
}

// ════════════════════════════════════════════════════════════════════════════
// SERIALIZATION - moved from persist.cpp
// ════════════════════════════════════════════════════════════════════════════

static std::string port_type_str(PortType t) {
     switch (t) {
         case PortType::V: return "V";
         case PortType::I: return "I";
         case PortType::Bool: return "Bool";
         case PortType::RPM: return "RPM";
         case PortType::Temperature: return "Temperature";
         case PortType::Pressure: return "Pressure";
         case PortType::Position: return "Position";
         case PortType::Any: return "Any";
    }
    spdlog::error("Unknown PortType enum value: {}", static_cast<int>(t));
    std::abort();
}

static std::optional<PortType> parse_port_type_str(const std::string& s) {
    if (s == "V") return PortType::V;
    if (s == "I") return PortType::I;
    if (s == "Bool") return PortType::Bool;
    if (s == "RPM") return PortType::RPM;
    if (s == "Temperature") return PortType::Temperature;
    if (s == "Pressure") return PortType::Pressure;
    if (s == "Position") return PortType::Position;
    if (s == "Any") return PortType::Any;
    spdlog::error("Unknown port type string: '{}'", s);
    return std::nullopt;
}

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

std::string Blueprint::to_simulator_json() const {
    json j = json::object();

    j["templates"] = json::object();

    json devices = json::array();
    std::set<std::string> emitted_ids;
    for (const auto& n : nodes) {
        if (n.expandable) continue;

        if (!emitted_ids.insert(n.id).second) {
            spdlog::warn("[dedup] Duplicate node '{}' on sim export", n.id);
            continue;
        }

        json device = json::object();
        device["name"] = n.id;
        device["template_name"] = "";
        device["classname"] = n.type_name;
        if (!n.render_hint.empty())
            device["render_hint"] = n.render_hint;
        if (n.expandable)
            device["expandable"] = true;
        device["priority"] = "med";
        device["bucket"] = nullptr;
        device["critical"] = false;
        device["ports"] = serialize_ports(n);
        
        if (!n.params.empty()) {
            json params = json::object();
            for (const auto& [key, value] : n.params) {
                params[key] = value;
            }
            device["params"] = params;
        }
        else if (n.type_name == "RefNode") {
            json params = json::object();
            float value = (n.node_content.type == NodeContentType::Value) ? n.node_content.value : 0.0f;
            params["value"] = (value == 0.0f) ? "0.0" : std::to_string(value);
            device["params"] = params;
        }

        devices.push_back(device);
    }
    j["devices"] = devices;

    std::set<std::string> blueprint_node_ids;
    for (const auto& n : nodes) {
        if (n.expandable)
            blueprint_node_ids.insert(n.id);
    }

    json connections = json::array();
    std::set<std::string> emitted_conn_keys;
    for (const auto& w : wires) {
        std::string from_node = w.start.node_id;
        std::string from_port = w.start.port_name;
        std::string to_node = w.end.node_id;
        std::string to_port = w.end.port_name;

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

    json editor = json::object();
    editor["viewport"] = {
        {"pan", {{"x", pan.x}, {"y", pan.y}}},
        {"zoom", zoom},
        {"grid_step", grid_step}
    };

    json editor_devices = json::array();
    for (const auto& n : nodes) {
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

    json node_states = json::object();
    for (const auto& n : nodes) {
        json node_state = {
            {"pos", {{"x", n.pos.x}, {"y", n.pos.y}}},
            {"size", {{"x", n.size.x}, {"y", n.size.y}}}
        };

        if (n.node_content.type != NodeContentType::None) {
            node_state["content"] = serialize_content(n);
        }

        node_states[n.id] = node_state;
    }
    editor["nodes"] = node_states;

    json wire_states = json::array();
    for (const auto& w : wires) {
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

    json sub_bp_json = json::array();
    std::set<std::string> saved_group_ids;
    for (const auto& group : sub_blueprint_instances) {
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
        sub_bp_json.push_back(group_json);
    }
    editor["sub_blueprint_instances"] = sub_bp_json;

    j["editor"] = editor;

    return j.dump(2);
}

// ════════════════════════════════════════════════════════════════════════════
// FLAT CONVERSION - Blueprint ↔ FlatBlueprint
// ════════════════════════════════════════════════════════════════════════════

static std::string content_type_to_string(NodeContentType t) {
    switch (t) {
        case NodeContentType::None:           return "none";
        case NodeContentType::Gauge:          return "gauge";
        case NodeContentType::Switch:         return "switch";
        case NodeContentType::VerticalToggle: return "vertical_toggle";
        case NodeContentType::Value:          return "value";
        case NodeContentType::Text:           return "text";
    }
    return "none";
}

static NodeContentType string_to_content_type(const std::string& s) {
    if (s == "gauge"   || s == "Gauge")          return NodeContentType::Gauge;
    if (s == "switch"  || s == "Switch")         return NodeContentType::Switch;
    if (s == "vertical_toggle" || s == "VerticalToggle") return NodeContentType::VerticalToggle;
    if (s == "value"   || s == "Value")          return NodeContentType::Value;
    if (s == "text"    || s == "Text")           return NodeContentType::Text;
    if (s == "HoldButton")     return NodeContentType::Switch;
    return NodeContentType::None;
}

static FlatNode node_to_flat(const Node& n) {
    FlatNode nv;
    nv.type = n.type_name;
    nv.pos = {n.pos.x, n.pos.y};
    nv.size = {n.size.x, n.size.y};

    for (const auto& [k, v] : n.params) {
        nv.params[k] = v;
    }

    if (n.node_content.type != NodeContentType::None) {
        FlatContent c;
        c.kind = content_type_to_string(n.node_content.type);
        c.label = n.node_content.label;
        c.value = n.node_content.value;
        c.min = n.node_content.min;
        c.max = n.node_content.max;
        c.unit = n.node_content.unit;
        c.state = n.node_content.state;
        nv.content = c;
    }

    if (n.color.has_value()) {
        FlatColor c;
        c.r = n.color->r;
        c.g = n.color->g;
        c.b = n.color->b;
        c.a = n.color->a;
        nv.color = c;
    }

    if (n.name != n.id) {
        nv.display_name = n.name;
    }
    nv.render_hint = n.render_hint;
    nv.expandable = n.expandable;
    nv.group_id = n.group_id;
    nv.blueprint_path = n.blueprint_path;

    return nv;
}

static FlatWire wire_to_flat(const Wire& w) {
    FlatWire wv;
    wv.id = w.id;
    wv.from = {w.start.node_id, w.start.port_name};
    wv.to = {w.end.node_id, w.end.port_name};
    for (const auto& pt : w.routing_points) {
        wv.routing.push_back({pt.x, pt.y});
    }
    return wv;
}

static FlatSubBlueprint sbi_to_flat(const SubBlueprintInstance& sbi, const Blueprint& bp) {
    FlatSubBlueprint sb;

    if (!sbi.blueprint_path.empty()) {
        sb.template_path = sbi.blueprint_path;
    }
    sb.type_name = sbi.type_name;
    sb.pos = {sbi.pos.x, sbi.pos.y};
    sb.size = {sbi.size.x, sbi.size.y};
    sb.collapsed = true;

    FlatOverrides ov;
    for (const auto& [k, v] : sbi.params_override) {
        ov.params[k] = v;
    }

    std::string prefix = sbi.id + ":";
    auto strip_prefix = [&](const std::string& nid) -> std::string {
        if (nid.size() > prefix.size() && nid.compare(0, prefix.size(), prefix) == 0)
            return nid.substr(prefix.size());
        return nid;
    };

    std::map<std::string, Pt> layout = sbi.layout_override;
    if (!sbi.baked_in) {
        for (const auto& nid : sbi.internal_node_ids) {
            if (const Node* n = bp.find_node(nid.c_str())) {
                layout[strip_prefix(nid)] = n->pos;
            }
        }
    }
    for (const auto& [k, pt] : layout) {
        ov.layout[k] = {pt.x, pt.y};
    }
    for (const auto& [k, pts] : sbi.internal_routing) {
        std::vector<FlatPos> v2pts;
        for (const auto& pt : pts) {
            v2pts.push_back({pt.x, pt.y});
        }
        ov.routing[k] = v2pts;
    }

    if (!ov.params.empty() || !ov.layout.empty() || !ov.routing.empty()) {
        sb.overrides = ov;
    }

    if (sbi.baked_in) {
        for (const auto& nid : sbi.internal_node_ids) {
            if (const Node* n = bp.find_node(nid.c_str())) {
                sb.nodes[strip_prefix(nid)] = node_to_flat(*n);
            }
        }
        std::set<std::string> internal_set(sbi.internal_node_ids.begin(),
                                           sbi.internal_node_ids.end());
        for (const auto& w : bp.wires) {
            if (internal_set.count(w.start.node_id) && internal_set.count(w.end.node_id)) {
                auto wv = wire_to_flat(w);
                wv.from.node = strip_prefix(w.start.node_id);
                wv.to.node = strip_prefix(w.end.node_id);
                sb.wires.push_back(std::move(wv));
            }
        }
    }

    return sb;
}

FlatBlueprint Blueprint::to_flat() const {
    FlatBlueprint bpv2;
    bpv2.version = 2;
    bpv2.meta.name = "";

    FlatViewport vp;
    vp.pan = {pan.x, pan.y};
    vp.zoom = zoom;
    vp.grid = grid_step;
    bpv2.viewport = vp;

    std::set<std::string> non_baked_in_internals;
    for (const auto& sbi : sub_blueprint_instances) {
        if (!sbi.baked_in) {
            for (const auto& nid : sbi.internal_node_ids) {
                non_baked_in_internals.insert(nid);
            }
        }
    }

    std::set<std::string> emitted_ids;
    for (const auto& n : nodes) {
        if (non_baked_in_internals.count(n.id) > 0) continue;

        if (!emitted_ids.insert(n.id).second) {
            spdlog::warn("[dedup] Duplicate node '{}' on editor save", n.id);
            continue;
        }

        bpv2.nodes[n.id] = node_to_flat(n);
    }

    std::set<std::string> emitted_wire_keys;
    for (const auto& w : wires) {
        if (non_baked_in_internals.count(w.start.node_id) > 0 &&
            non_baked_in_internals.count(w.end.node_id) > 0) {
            continue;
        }
        if (!emitted_ids.count(w.start.node_id) || !emitted_ids.count(w.end.node_id))
            continue;

        std::string key = w.start.node_id + "." + w.start.port_name + "→" +
                          w.end.node_id + "." + w.end.port_name;
        if (!emitted_wire_keys.insert(key).second) {
            spdlog::warn("[dedup] Duplicate wire on save: {}", key);
            continue;
        }

        bpv2.wires.push_back(wire_to_flat(w));
    }

    std::set<std::string> emitted_group_ids;
    for (const auto& sbi : sub_blueprint_instances) {
        if (!emitted_group_ids.insert(sbi.id).second) continue;
        bpv2.sub_blueprints[sbi.id] = sbi_to_flat(sbi, *this);
    }

    return bpv2;
}

std::optional<Blueprint> Blueprint::from_flat(const ::FlatBlueprint& bpv2) {
    Blueprint bp;

    if (bpv2.viewport.has_value()) {
        bp.pan = Pt(bpv2.viewport->pan[0], bpv2.viewport->pan[1]);
        bp.zoom = bpv2.viewport->zoom;
        bp.grid_step = bpv2.viewport->grid;
    }

    std::set<std::string> loaded_ids;
    for (const auto& [id, nv] : bpv2.nodes) {
        if (!loaded_ids.insert(id).second) {
            spdlog::warn("[dedup] Duplicate node '{}' on load — skipping", id);
            continue;
        }

        Node n;
        n.id = id;
        n.type_name = nv.type;
        n.name = nv.display_name.empty() ? id : nv.display_name;
        n.render_hint = nv.render_hint;
        n.expandable = nv.expandable;
        n.group_id = nv.group_id;
        n.blueprint_path = nv.blueprint_path;
        if (n.expandable && !n.blueprint_path.empty()) {
            n.collapsed = true;
        }

        n.pos = Pt(nv.pos[0], nv.pos[1]);
        n.size = Pt(nv.size[0], nv.size[1]);
        if (nv.size[0] != 0.0f || nv.size[1] != 0.0f) {
            n.size_explicitly_set = true;
        }

        for (const auto& [k, v] : nv.params) {
            n.params[k] = v;
        }

        if (nv.content.has_value()) {
            n.node_content.type = string_to_content_type(nv.content->kind);
            n.node_content.label = nv.content->label;
            n.node_content.value = nv.content->value;
            n.node_content.min = nv.content->min;
            n.node_content.max = nv.content->max;
            n.node_content.unit = nv.content->unit;
            n.node_content.state = nv.content->state;
        }

        if (nv.color.has_value()) {
            NodeColor c;
            c.r = nv.color->r;
            c.g = nv.color->g;
            c.b = nv.color->b;
            c.a = nv.color->a;
            n.color = c;
        }

        bp.nodes.push_back(std::move(n));
    }

    static TypeRegistry registry = load_type_registry();
    for (auto& n : bp.nodes) {
        const auto* def = registry.get(n.type_name);
        if (def) {
            for (const auto& [port_name, port_def] : def->ports) {
                EditorPort p;
                p.name = port_name;
                p.type = port_def.type;
                if (port_def.direction == PortDirection::In) {
                    p.side = PortSide::Input;
                    n.inputs.push_back(p);
                } else if (port_def.direction == PortDirection::Out) {
                    p.side = PortSide::Output;
                    n.outputs.push_back(p);
                } else if (port_def.direction == PortDirection::InOut) {
                    p.side = PortSide::Input;
                    n.inputs.push_back(p);
                    EditorPort p_out = p;
                    p_out.side = PortSide::Output;
                    n.outputs.push_back(p_out);
                }
            }

            for (const auto& [key, value] : def->params) {
                if (n.params.find(key) == n.params.end()) {
                    n.params[key] = value;
                }
            }

            if (n.render_hint.empty()) {
                n.render_hint = def->render_hint;
            }
        }
    }

    std::set<std::string> loaded_wire_keys;
    for (const auto& wv : bpv2.wires) {
        std::string key = wv.from.node + "." + wv.from.port + "→" +
                          wv.to.node + "." + wv.to.port;
        if (!loaded_wire_keys.insert(key).second) {
            spdlog::warn("[dedup] Duplicate wire on load: {}", key);
            continue;
        }

        Wire w;
        w.id = wv.id.empty() ? key : wv.id;
        w.start.node_id = wv.from.node;
        w.start.port_name = wv.from.port;
        w.start.side = PortSide::Output;
        w.end.node_id = wv.to.node;
        w.end.port_name = wv.to.port;
        w.end.side = PortSide::Input;

        for (const auto& pt : wv.routing) {
            w.routing_points.push_back(Pt(pt[0], pt[1]));
        }

        bp.wires.push_back(std::move(w));
    }
    bp.rebuild_wire_index();

    std::set<std::string> loaded_group_ids;
    for (const auto& [id, sb] : bpv2.sub_blueprints) {
        if (!loaded_group_ids.insert(id).second) continue;

        SubBlueprintInstance sbi;
        sbi.id = id;
        sbi.blueprint_path = sb.template_path.value_or("");
        sbi.type_name = sb.type_name;
        sbi.baked_in = sb.is_embedded();
        sbi.pos = Pt(sb.pos[0], sb.pos[1]);
        sbi.size = Pt(sb.size[0], sb.size[1]);

        for (const auto& n : bp.nodes) {
            if (n.group_id == id) {
                sbi.internal_node_ids.push_back(n.id);
            }
        }

        if (sb.overrides.has_value()) {
            for (const auto& [k, v] : sb.overrides->params) {
                sbi.params_override[k] = v;
            }
            for (const auto& [k, pos] : sb.overrides->layout) {
                sbi.layout_override[k] = Pt(pos[0], pos[1]);
            }
            for (const auto& [k, pts] : sb.overrides->routing) {
                std::vector<Pt> rpts;
                for (const auto& pt : pts) {
                    rpts.push_back(Pt(pt[0], pt[1]));
                }
                sbi.internal_routing[k] = rpts;
            }
        }

        for (const auto& n : bp.nodes) {
            if (n.id == id && n.expandable) {
                sbi.pos = n.pos;
                sbi.size = n.size;
                break;
            }
        }

        bp.sub_blueprint_instances.push_back(std::move(sbi));
    }

    for (auto& sbi : bp.sub_blueprint_instances) {
        if (sbi.baked_in) continue;

        const auto* def = registry.get(sbi.type_name);
        if (!def) {
            spdlog::warn("[persist] Cannot re-expand '{}': type '{}' not in registry",
                         sbi.id, sbi.type_name);
            continue;
        }

        Blueprint sub_bp = expand_type_definition(*def, registry);

        std::vector<std::string> internal_ids;
        for (auto& node : sub_bp.nodes) {
            node.id = sbi.id + ":" + node.id;
            node.name = node.id;
            node.group_id = sbi.id;
            internal_ids.push_back(node.id);

            std::string local_id = node.id.substr(sbi.id.size() + 1);
            auto it = sbi.layout_override.find(local_id);
            if (it != sbi.layout_override.end())
                node.pos = it->second;

            bp.nodes.push_back(std::move(node));
        }

        for (const auto& [key, value] : sbi.params_override) {
            auto dot = key.find('.');
            if (dot == std::string::npos) continue;
            std::string local_id = sbi.id + ":" + key.substr(0, dot);
            std::string param = key.substr(dot + 1);
            if (Node* n = bp.find_node(local_id.c_str()))
                n->params[param] = value;
        }

        for (auto& wire : sub_bp.wires) {
            wire.start.node_id = sbi.id + ":" + wire.start.node_id;
            wire.end.node_id = sbi.id + ":" + wire.end.node_id;
            wire.id = sbi.id + ":" + wire.id;
            bp.wires.push_back(std::move(wire));
        }

        sbi.internal_node_ids = internal_ids;

        if (sbi.layout_override.empty()) {
            bool all_zero = true;
            for (const auto& nid : internal_ids) {
                if (const Node* n = bp.find_node(nid.c_str())) {
                    if (n->pos.x != 0.0f || n->pos.y != 0.0f) {
                        all_zero = false;
                        break;
                    }
                }
            }
            if (all_zero && !internal_ids.empty())
                bp.auto_layout_group(sbi.id);
        }
    }
    bp.rebuild_wire_index();

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

std::string Blueprint::serialize() const {
    auto bpv2 = to_flat();
    return serialize_flat_blueprint(bpv2);
}

std::optional<Blueprint> Blueprint::deserialize(const std::string& json_str) {
    auto bpv2 = parse_flat_blueprint(json_str);
    if (bpv2.has_value()) {
        return from_flat(*bpv2);
    }

    spdlog::error("Blueprint::deserialize: not a valid v2 blueprint (version field missing or != 2)");
    return std::nullopt;
}
