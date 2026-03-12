#include "visual/scene/persist.h"
#include "router/router.h"
#include "json_parser/json_parser.h"
#include "data/node.h"
#include "editor/data/flat_blueprint.h"
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

// BUGFIX [e2c8d4] Return optional instead of aborting on unknown port type.
// A single malformed blueprint file should not crash the entire application.
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
// v2 format: uses BlueprintV2 as the serialization schema.
// blueprint_to_json() is only for simulation (rewrites wires, skips Blueprint kind).

// ─── Conversion helpers: Editor Blueprint ↔ BlueprintV2 ───────────────────

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
    if (s == "HoldButton")     return NodeContentType::Switch;  // HoldButton uses Switch type
    return NodeContentType::None;
}

/// Convert editor Node → v2 NodeV2
static FlatNode node_to_flat(const Node& n) {
    FlatNode nv;
    nv.type = n.type_name;
    nv.pos = {n.pos.x, n.pos.y};
    nv.size = {n.size.x, n.size.y};

    // Params
    for (const auto& [k, v] : n.params) {
        nv.params[k] = v;
    }

    // Content
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

    // Color
    if (n.color.has_value()) {
        FlatColor c;
        c.r = n.color->r;
        c.g = n.color->g;
        c.b = n.color->b;
        c.a = n.color->a;
        nv.color = c;
    }

    // Editor-only fields
    if (n.name != n.id) {
        nv.display_name = n.name;
    }
    nv.render_hint = n.render_hint;
    nv.expandable = n.expandable;
    nv.group_id = n.group_id;
    nv.blueprint_path = n.blueprint_path;

    return nv;
}

/// Convert editor Wire → v2 WireV2
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

/// Convert editor SubBlueprintInstance → v2 SubBlueprintV2
static FlatSubBlueprint sbi_to_flat(const SubBlueprintInstance& sbi,
                                            const Blueprint& bp) {
    FlatSubBlueprint sb;

    if (!sbi.blueprint_path.empty()) {
        sb.template_path = sbi.blueprint_path;
    }
    sb.type_name = sbi.type_name;
    sb.pos = {sbi.pos.x, sbi.pos.y};
    sb.size = {sbi.size.x, sbi.size.y};
    sb.collapsed = true;  // Currently always collapsed in save

    // Build overrides
    FlatOverrides ov;
    for (const auto& [k, v] : sbi.params_override) {
        ov.params[k] = v;
    }

    // Strip "sbi_id:" prefix from node ID, or return as-is if mismatched
    std::string prefix = sbi.id + ":";
    auto strip_prefix = [&](const std::string& nid) -> std::string {
        if (nid.size() > prefix.size() && nid.compare(0, prefix.size(), prefix) == 0)
            return nid.substr(prefix.size());
        return nid;  // Already unprefixed or mismatched
    };

    // For non-baked-in SBIs, snapshot live Node::pos into layout
    // Override keys are UNPREFIXED (per v2 design: scope is structural)
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

    // For baked-in sub-blueprints, store internal nodes/wires inline
    // Node keys and wire endpoints are UNPREFIXED (per v2 design)
    if (sbi.baked_in) {
        for (const auto& nid : sbi.internal_node_ids) {
            if (const Node* n = bp.find_node(nid.c_str())) {
                sb.nodes[strip_prefix(nid)] = node_to_flat(*n);
            }
        }
        // Collect wires where both endpoints are internal
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

/// Convert full editor Blueprint → BlueprintV2 (for editor save)
static FlatBlueprint editor_blueprint_to_flat(const Blueprint& bp) {
    FlatBlueprint bpv2;
    bpv2.version = 2;

    // Meta (minimal for editor saves — no component-level metadata)
    bpv2.meta.name = "";

    // Viewport
    FlatViewport vp;
    vp.pan = {bp.pan.x, bp.pan.y};
    vp.zoom = bp.zoom;
    vp.grid = bp.grid_step;
    bpv2.viewport = vp;

    // Build set of internal node IDs for non-baked-in sub-blueprints
    // These nodes should NOT be saved — they will be re-expanded from library on load
    std::set<std::string> non_baked_in_internals;
    for (const auto& sbi : bp.sub_blueprint_instances) {
        if (!sbi.baked_in) {
            for (const auto& nid : sbi.internal_node_ids) {
                non_baked_in_internals.insert(nid);
            }
        }
    }

    // Nodes
    std::set<std::string> emitted_ids;
    for (const auto& n : bp.nodes) {
        // Skip internal nodes of non-baked-in sub-blueprints
        if (non_baked_in_internals.count(n.id) > 0) continue;

        // Dedup
        if (!emitted_ids.insert(n.id).second) {
            spdlog::warn("[dedup] Duplicate node '{}' on editor save", n.id);
            continue;
        }

        bpv2.nodes[n.id] = node_to_flat(n);
    }

    // Wires
    std::set<std::string> emitted_wire_keys;
    for (const auto& w : bp.wires) {
        // Skip wires where BOTH endpoints are internal nodes of non-baked-in sub-blueprints
        if (non_baked_in_internals.count(w.start.node_id) > 0 &&
            non_baked_in_internals.count(w.end.node_id) > 0) {
            continue;
        }
        // Skip wires where either endpoint wasn't emitted
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

    // Sub-blueprints
    std::set<std::string> emitted_group_ids;
    for (const auto& sbi : bp.sub_blueprint_instances) {
        if (!emitted_group_ids.insert(sbi.id).second) continue;
        bpv2.sub_blueprints[sbi.id] = sbi_to_flat(sbi, bp);
    }

    return bpv2;
}

/// Convert v2 BlueprintV2 → editor Blueprint (for editor load)
static std::optional<Blueprint> flat_to_editor_blueprint(const FlatBlueprint& bpv2) {
    Blueprint bp;

    // Viewport
    if (bpv2.viewport.has_value()) {
        bp.pan = Pt(bpv2.viewport->pan[0], bpv2.viewport->pan[1]);
        bp.zoom = bpv2.viewport->zoom;
        bp.grid_step = bpv2.viewport->grid;
    }

    // Nodes
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

        // Params
        for (const auto& [k, v] : nv.params) {
            n.params[k] = v;
        }

        // Content
        if (nv.content.has_value()) {
            n.node_content.type = string_to_content_type(nv.content->kind);
            n.node_content.label = nv.content->label;
            n.node_content.value = nv.content->value;
            n.node_content.min = nv.content->min;
            n.node_content.max = nv.content->max;
            n.node_content.unit = nv.content->unit;
            n.node_content.state = nv.content->state;
        }

        // Color
        if (nv.color.has_value()) {
            NodeColor c;
            c.r = nv.color->r;
            c.g = nv.color->g;
            c.b = nv.color->b;
            c.a = nv.color->a;
            n.color = c;
        }

        // Ports are NOT stored in v2 editor save — they are enriched from registry below
        bp.nodes.push_back(std::move(n));
    }

    // Enrich nodes from type registry (ports, params, render_hint)
    static TypeRegistry registry = load_type_registry();
    for (auto& n : bp.nodes) {
        const auto* def = registry.get(n.type_name);
        if (def) {
            // Reconstruct ports from registry
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
                    // InOut: add to both inputs and outputs
                    p.side = PortSide::Input;
                    n.inputs.push_back(p);
                    EditorPort p_out = p;
                    p_out.side = PortSide::Output;
                    n.outputs.push_back(p_out);
                }
            }

            // Fill missing params from defaults
            for (const auto& [key, value] : def->params) {
                if (n.params.find(key) == n.params.end()) {
                    n.params[key] = value;
                }
            }

            // Enrich render_hint if not saved (old saves)
            if (n.render_hint.empty()) {
                n.render_hint = def->render_hint;
            }
        }
    }

    // Wires
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

    // Sub-blueprints
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

        // Build internal_node_ids from group_id on nodes
        for (const auto& n : bp.nodes) {
            if (n.group_id == id) {
                sbi.internal_node_ids.push_back(n.id);
            }
        }

        // Overrides
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

        // If no expandable node found but we have pos/size from JSON, keep them
        // (pos/size were already set from sb above)
        // But if there's an expandable node, use its position as source of truth
        for (const auto& n : bp.nodes) {
            if (n.id == id && n.expandable) {
                sbi.pos = n.pos;
                sbi.size = n.size;
                break;
            }
        }

        bp.sub_blueprint_instances.push_back(std::move(sbi));
    }

    // Re-expand non-baked-in sub-blueprints from library
    for (auto& sbi : bp.sub_blueprint_instances) {
        if (sbi.baked_in) continue;

        const auto* def = registry.get(sbi.type_name);
        if (!def) {
            spdlog::warn("[persist] Cannot re-expand '{}': type '{}' not in registry",
                         sbi.id, sbi.type_name);
            continue;
        }

        Blueprint sub_bp = expand_type_definition(*def, registry);

        // Prefix node IDs with sbi.id + ":"
        std::vector<std::string> internal_ids;
        for (auto& node : sub_bp.nodes) {
            node.id = sbi.id + ":" + node.id;
            node.name = node.id;
            node.group_id = sbi.id;
            internal_ids.push_back(node.id);

            // Apply layout_override if present (keys are unprefixed per v2 design)
            std::string local_id = node.id.substr(sbi.id.size() + 1);
            auto it = sbi.layout_override.find(local_id);
            if (it != sbi.layout_override.end())
                node.pos = it->second;

            bp.nodes.push_back(std::move(node));
        }

        // Apply params_override: key format "local_node_id.param_name"
        for (const auto& [key, value] : sbi.params_override) {
            auto dot = key.find('.');
            if (dot == std::string::npos) continue;
            std::string local_id = sbi.id + ":" + key.substr(0, dot);
            std::string param = key.substr(dot + 1);
            if (Node* n = bp.find_node(local_id.c_str()))
                n->params[param] = value;
        }

        // Prefix wire IDs
        for (auto& wire : sub_bp.wires) {
            wire.start.node_id = sbi.id + ":" + wire.start.node_id;
            wire.end.node_id = sbi.id + ":" + wire.end.node_id;
            wire.id = sbi.id + ":" + wire.id;
            bp.wires.push_back(std::move(wire));
        }

        sbi.internal_node_ids = internal_ids;

        // Fallback auto-layout if no layout_override was saved
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

std::string blueprint_to_editor_json(const Blueprint& bp) {
    auto bpv2 = editor_blueprint_to_flat(bp);
    return serialize_flat_blueprint(bpv2);
}

// BUGFIX [f2c8d4] Removed ~200 lines of dead code:
// - device_to_node() — legacy JSON→Node converter, unused since editor format migration
// - device_instance_to_node() — DeviceInstance→Node converter, no callers
// - create_node_content() — duplicate wrapper, replaced by create_node_content_from_def
// - auto_layout() / port_center() / classify_node() / NodeRole — never called
// - snap() / LAYOUT_GRID — only used by dead auto_layout
// [Phase 3] Removed v1 load_editor_format() and helpers — replaced by flat_to_editor_blueprint()

std::optional<Blueprint> blueprint_from_json(const std::string& json_str) {
    // Try v2 format first
    auto bpv2 = parse_flat_blueprint(json_str);
    if (bpv2.has_value()) {
        return flat_to_editor_blueprint(*bpv2);
    }

    // v2 parse failed — this is not a valid v2 blueprint
    spdlog::error("blueprint_from_json: not a valid v2 blueprint (version field missing or != 2)");
    return std::nullopt;
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
