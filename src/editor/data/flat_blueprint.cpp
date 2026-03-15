/// Flat Blueprint parser and serializer
///
/// Implements the "Everything is a Blueprint" unified JSON format.
/// See JSON_FORMAT_V2_DESIGN.md for schema specification.

#include "editor/data/flat_blueprint.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

// ==================================================================
// Parse helpers
// ==================================================================

static FlatPos parse_pos(const json& j) {
    if (j.is_array() && j.size() >= 2) {
        return {j[0].get<float>(), j[1].get<float>()};
    }
    return {0.0f, 0.0f};
}

static FlatPort parse_exposed_port(const json& j) {
    FlatPort ep;
    if (j.contains("direction")) ep.direction = j["direction"].get<std::string>();
    if (j.contains("type"))      ep.type = j["type"].get<std::string>();
    if (j.contains("alias"))     ep.alias = j["alias"].get<std::string>();
    return ep;
}

static FlatParam parse_param_def(const json& j) {
    FlatParam pd;
    if (j.contains("type"))    pd.type = j["type"].get<std::string>();
    if (j.contains("default")) pd.default_val = j["default"].get<std::string>();
    return pd;
}

static FlatContent parse_content(const json& j) {
    FlatContent c;
    if (j.contains("kind"))  c.kind = j["kind"].get<std::string>();
    if (j.contains("label")) c.label = j["label"].get<std::string>();
    if (j.contains("value")) c.value = j["value"].get<float>();
    if (j.contains("min"))   c.min = j["min"].get<float>();
    if (j.contains("max"))   c.max = j["max"].get<float>();
    if (j.contains("unit"))  c.unit = j["unit"].get<std::string>();
    if (j.contains("state")) c.state = j["state"].get<bool>();
    return c;
}

static FlatColor parse_color(const json& j) {
    FlatColor c;
    if (j.contains("r")) c.r = j["r"].get<float>();
    if (j.contains("g")) c.g = j["g"].get<float>();
    if (j.contains("b")) c.b = j["b"].get<float>();
    if (j.contains("a")) c.a = j["a"].get<float>();
    return c;
}

static FlatNode parse_node(const json& j) {
    FlatNode n;
    if (j.contains("type")) n.type = j["type"].get<std::string>();
    if (j.contains("pos"))  n.pos = parse_pos(j["pos"]);
    if (j.contains("size")) n.size = parse_pos(j["size"]);

    if (j.contains("params")) {
        for (auto& [k, v] : j["params"].items()) {
            n.params[k] = v.get<std::string>();
        }
    }

    if (j.contains("content")) n.content = parse_content(j["content"]);
    if (j.contains("color") && j["color"].is_object())   n.color = parse_color(j["color"]);

    // Editor-only fields
    if (j.contains("display_name"))   n.display_name = j["display_name"].get<std::string>();
    if (j.contains("render_hint"))    n.render_hint = j["render_hint"].get<std::string>();
    if (j.contains("expandable"))     n.expandable = j["expandable"].get<bool>();
    if (j.contains("group_id"))       n.group_id = j["group_id"].get<std::string>();
    if (j.contains("blueprint_path")) n.blueprint_path = j["blueprint_path"].get<std::string>();

    return n;
}

static FlatWireEnd parse_wire_end(const json& j) {
    FlatWireEnd we;
    if (j.is_array() && j.size() >= 2) {
        we.node = j[0].get<std::string>();
        we.port = j[1].get<std::string>();
    }
    return we;
}

static FlatWire parse_wire(const json& j) {
    FlatWire w;
    if (j.contains("id"))   w.id = j["id"].get<std::string>();
    if (j.contains("from")) w.from = parse_wire_end(j["from"]);
    if (j.contains("to"))   w.to = parse_wire_end(j["to"]);

    if (j.contains("routing") && j["routing"].is_array()) {
        for (const auto& pt : j["routing"]) {
            w.routing.push_back(parse_pos(pt));
        }
    }

    return w;
}

static std::map<std::string, FlatNode> parse_nodes_map(const json& j) {
    std::map<std::string, FlatNode> nodes;
    if (j.is_object()) {
        for (auto& [id, node_j] : j.items()) {
            nodes[id] = parse_node(node_j);
        }
    }
    return nodes;
}

static std::vector<FlatWire> parse_wires_array(const json& j) {
    std::vector<FlatWire> wires;
    if (j.is_array()) {
        for (const auto& w_j : j) {
            wires.push_back(parse_wire(w_j));
        }
    }
    return wires;
}

static FlatOverrides parse_overrides(const json& j) {
    FlatOverrides ov;

    if (j.contains("params") && j["params"].is_object()) {
        for (auto& [k, v] : j["params"].items()) {
            ov.params[k] = v.get<std::string>();
        }
    }

    if (j.contains("layout") && j["layout"].is_object()) {
        for (auto& [k, v] : j["layout"].items()) {
            ov.layout[k] = parse_pos(v);
        }
    }

    if (j.contains("routing") && j["routing"].is_object()) {
        for (auto& [k, v] : j["routing"].items()) {
            std::vector<FlatPos> points;
            if (v.is_array()) {
                for (const auto& pt : v) {
                    points.push_back(parse_pos(pt));
                }
            }
            ov.routing[k] = std::move(points);
        }
    }

    return ov;
}

static FlatSubBlueprint parse_sub_blueprint(const json& j) {
    FlatSubBlueprint sb;

    if (j.contains("template"))  sb.template_path = j["template"].get<std::string>();
    if (j.contains("type_name")) sb.type_name = j["type_name"].get<std::string>();
    if (j.contains("pos"))       sb.pos = parse_pos(j["pos"]);
    if (j.contains("size"))      sb.size = parse_pos(j["size"]);
    if (j.contains("collapsed")) sb.collapsed = j["collapsed"].get<bool>();

    if (j.contains("overrides")) sb.overrides = parse_overrides(j["overrides"]);
    if (j.contains("nodes"))     sb.nodes = parse_nodes_map(j["nodes"]);
    if (j.contains("wires"))     sb.wires = parse_wires_array(j["wires"]);

    return sb;
}

static FlatMeta parse_meta(const json& j) {
    FlatMeta m;
    if (j.contains("name"))        m.name = j["name"].get<std::string>();
    if (j.contains("description")) m.description = j["description"].get<std::string>();
    if (j.contains("cpp_class"))   m.cpp_class = j["cpp_class"].get<bool>();

    if (j.contains("domains") && j["domains"].is_array()) {
        for (const auto& d : j["domains"]) {
            m.domains.push_back(d.get<std::string>());
        }
    }

    if (j.contains("priority"))     m.priority = j["priority"].get<std::string>();
    if (j.contains("critical"))     m.critical = j["critical"].get<bool>();
    if (j.contains("content_type")) m.content_type = j["content_type"].get<std::string>();
    if (j.contains("render_hint"))  m.render_hint = j["render_hint"].get<std::string>();
    if (j.contains("visual_only"))  m.visual_only = j["visual_only"].get<bool>();
    if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
        m.size = FlatPos{j["size"][0].get<float>(), j["size"][1].get<float>()};
    }

    return m;
}

static FlatViewport parse_viewport(const json& j) {
    FlatViewport vp;
    if (j.contains("pan"))  vp.pan = parse_pos(j["pan"]);
    if (j.contains("zoom")) vp.zoom = j["zoom"].get<float>();
    if (j.contains("grid")) vp.grid = j["grid"].get<float>();
    return vp;
}

// ==================================================================
// parse_flat_blueprint
// ==================================================================

std::optional<FlatBlueprint> parse_flat_blueprint(const std::string& json_text) {
    json j;
    try {
        j = json::parse(json_text);
    } catch (const json::parse_error& e) {
        spdlog::error("[flat] JSON parse error: {}", e.what());
        return std::nullopt;
    }

    // Version gate
    if (!j.contains("version") || !j["version"].is_number_integer()) {
        spdlog::error("[flat] Missing or non-integer 'version' field");
        return std::nullopt;
    }
    int version = j["version"].get<int>();
    if (version != 2) {
        spdlog::error("[flat] Unsupported version: {} (expected 2)", version);
        return std::nullopt;
    }

    FlatBlueprint bp;
    bp.version = 2;

    // Meta (required)
    if (j.contains("meta")) {
        bp.meta = parse_meta(j["meta"]);
    }

    // Exposes
    if (j.contains("exposes") && j["exposes"].is_object()) {
        for (auto& [name, port_j] : j["exposes"].items()) {
            bp.exposes[name] = parse_exposed_port(port_j);
        }
    }

    // Params (cpp_class only)
    if (j.contains("params") && j["params"].is_object()) {
        for (auto& [name, param_j] : j["params"].items()) {
            bp.params[name] = parse_param_def(param_j);
        }
    }

    // Nodes
    if (j.contains("nodes")) {
        bp.nodes = parse_nodes_map(j["nodes"]);
    }

    // Wires
    if (j.contains("wires")) {
        bp.wires = parse_wires_array(j["wires"]);
    }

    // Sub-blueprints
    if (j.contains("sub_blueprints") && j["sub_blueprints"].is_object()) {
        for (auto& [id, sb_j] : j["sub_blueprints"].items()) {
            bp.sub_blueprints[id] = parse_sub_blueprint(sb_j);
        }
    }

    // Viewport
    if (j.contains("viewport")) {
        bp.viewport = parse_viewport(j["viewport"]);
    }

    return bp;
}

// ==================================================================
// Serialize helpers
// ==================================================================

static json serialize_pos(const FlatPos& p) {
    return json::array({p[0], p[1]});
}

static json serialize_exposed_port(const FlatPort& ep) {
    json j = {{"direction", ep.direction}, {"type", ep.type}};
    if (ep.alias.has_value()) {
        j["alias"] = *ep.alias;
    }
    return j;
}

static json serialize_param_def(const FlatParam& pd) {
    return {{"type", pd.type}, {"default", pd.default_val}};
}

static json serialize_content(const FlatContent& c) {
    json j = {
        {"kind", c.kind},
        {"label", c.label},
        {"value", c.value},
        {"min", c.min},
        {"max", c.max},
        {"unit", c.unit}
    };
    if (c.state) {
        j["state"] = c.state;
    }
    return j;
}

static json serialize_color(const FlatColor& c) {
    return {{"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a}};
}

static json serialize_node(const FlatNode& n) {
    json j;
    j["type"] = n.type;

    if (n.pos[0] != 0.0f || n.pos[1] != 0.0f) {
        j["pos"] = serialize_pos(n.pos);
    }
    if (n.size[0] != 0.0f || n.size[1] != 0.0f) {
        j["size"] = serialize_pos(n.size);
    }
    if (!n.params.empty()) {
        j["params"] = n.params;
    }
    if (n.content.has_value()) {
        j["content"] = serialize_content(*n.content);
    }
    if (n.color.has_value()) {
        j["color"] = serialize_color(*n.color);
    }

    // Editor-only fields (emitted only when non-default/non-empty)
    if (!n.display_name.empty()) {
        j["display_name"] = n.display_name;
    }
    if (!n.render_hint.empty()) {
        j["render_hint"] = n.render_hint;
    }
    if (n.expandable) {
        j["expandable"] = true;
    }
    if (!n.group_id.empty()) {
        j["group_id"] = n.group_id;
    }
    if (!n.blueprint_path.empty()) {
        j["blueprint_path"] = n.blueprint_path;
    }

    return j;
}

static json serialize_wire_end(const FlatWireEnd& we) {
    return json::array({we.node, we.port});
}

static json serialize_wire(const FlatWire& w) {
    json j;
    j["id"] = w.id;
    j["from"] = serialize_wire_end(w.from);
    j["to"] = serialize_wire_end(w.to);

    if (!w.routing.empty()) {
        json routing = json::array();
        for (const auto& pt : w.routing) {
            routing.push_back(serialize_pos(pt));
        }
        j["routing"] = routing;
    }

    return j;
}

static json serialize_overrides(const FlatOverrides& ov) {
    json j;

    if (!ov.params.empty()) {
        j["params"] = ov.params;
    }
    if (!ov.layout.empty()) {
        json layout;
        for (const auto& [k, v] : ov.layout) {
            layout[k] = serialize_pos(v);
        }
        j["layout"] = layout;
    }
    if (!ov.routing.empty()) {
        json routing;
        for (const auto& [k, points] : ov.routing) {
            json arr = json::array();
            for (const auto& pt : points) {
                arr.push_back(serialize_pos(pt));
            }
            routing[k] = arr;
        }
        j["routing"] = routing;
    }

    return j;
}

static json serialize_sub_blueprint(const FlatSubBlueprint& sb) {
    json j;

    if (sb.template_path.has_value()) {
        j["template"] = *sb.template_path;
    }
    if (!sb.type_name.empty()) {
        j["type_name"] = sb.type_name;
    }

    if (sb.pos[0] != 0.0f || sb.pos[1] != 0.0f) {
        j["pos"] = serialize_pos(sb.pos);
    }
    if (sb.size[0] != 0.0f || sb.size[1] != 0.0f) {
        j["size"] = serialize_pos(sb.size);
    }
    j["collapsed"] = sb.collapsed;

    if (sb.overrides.has_value()) {
        j["overrides"] = serialize_overrides(*sb.overrides);
    }

    if (!sb.nodes.empty()) {
        json nodes;
        for (const auto& [id, node] : sb.nodes) {
            nodes[id] = serialize_node(node);
        }
        j["nodes"] = nodes;
    }

    if (!sb.wires.empty()) {
        json wires = json::array();
        for (const auto& w : sb.wires) {
            wires.push_back(serialize_wire(w));
        }
        j["wires"] = wires;
    }

    return j;
}

// ==================================================================
// serialize_flat_blueprint
// ==================================================================

std::string serialize_flat_blueprint(const FlatBlueprint& bp) {
    json j;

    j["version"] = bp.version;

    // Meta
    {
        json meta;
        meta["name"] = bp.meta.name;
        if (!bp.meta.description.empty()) {
            meta["description"] = bp.meta.description;
        }
        if (!bp.meta.domains.empty()) {
            meta["domains"] = bp.meta.domains;
        }
        if (bp.meta.cpp_class) {
            meta["cpp_class"] = bp.meta.cpp_class;
        }
        if (bp.meta.priority != "med") {
            meta["priority"] = bp.meta.priority;
        }
        if (bp.meta.critical) {
            meta["critical"] = bp.meta.critical;
        }
        if (bp.meta.content_type != "None") {
            meta["content_type"] = bp.meta.content_type;
        }
        if (!bp.meta.render_hint.empty()) {
            meta["render_hint"] = bp.meta.render_hint;
        }
        if (bp.meta.visual_only) {
            meta["visual_only"] = bp.meta.visual_only;
        }
        if (bp.meta.size.has_value()) {
            meta["size"] = serialize_pos(*bp.meta.size);
        }
        j["meta"] = meta;
    }

    // Exposes
    if (!bp.exposes.empty()) {
        json exposes;
        for (const auto& [name, ep] : bp.exposes) {
            exposes[name] = serialize_exposed_port(ep);
        }
        j["exposes"] = exposes;
    }

    // Params
    if (!bp.params.empty()) {
        json params;
        for (const auto& [name, pd] : bp.params) {
            params[name] = serialize_param_def(pd);
        }
        j["params"] = params;
    }

    // Viewport (root docs only)
    if (bp.viewport.has_value()) {
        json vp;
        vp["pan"] = serialize_pos(bp.viewport->pan);
        vp["zoom"] = bp.viewport->zoom;
        vp["grid"] = bp.viewport->grid;
        j["viewport"] = vp;
    }

    // Nodes
    if (!bp.nodes.empty()) {
        json nodes;
        for (const auto& [id, node] : bp.nodes) {
            nodes[id] = serialize_node(node);
        }
        j["nodes"] = nodes;
    }

    // Wires
    if (!bp.wires.empty()) {
        json wires = json::array();
        for (const auto& w : bp.wires) {
            wires.push_back(serialize_wire(w));
        }
        j["wires"] = wires;
    }

    // Sub-blueprints
    if (!bp.sub_blueprints.empty()) {
        json subs;
        for (const auto& [id, sb] : bp.sub_blueprints) {
            subs[id] = serialize_sub_blueprint(sb);
        }
        j["sub_blueprints"] = subs;
    }

    return j.dump(2);
}
