#include "extract_blueprint.h"

#include "editor/visual/persist.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace editor::commands {

namespace {

struct ExternalConnection {
    bool is_input = false;
    ui::InternedId external_node_id;
    ui::InternedId external_port;
    ui::InternedId internal_node_id;
    ui::InternedId internal_port;
    std::string iface_name;
    Domain domain = Domain::Electrical;
    ui::InternedId original_wire_id;
};

struct ExtractionPlan {
    std::vector<bp2::Blueprint::Node> internal_nodes;
    std::vector<bp2::Blueprint::Wire> internal_wires;
    std::vector<ExternalConnection> inputs;
    std::vector<ExternalConnection> outputs;
    std::unordered_set<ui::InternedId> selected_set;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;
};

static PortType port_type_for_domain(Domain d) {
    switch (d) {
        case Domain::Electrical: return PortType::V;
        case Domain::Logical: return PortType::Bool;
        case Domain::Mechanical: return PortType::RPM;
        case Domain::Hydraulic: return PortType::Pressure;
        case Domain::Thermal: return PortType::Temperature;
    }
    return PortType::Any;
}

static bool path_to_node_port(const bp2::Path& path,
                              const bp2::PathArena& arena,
                              ui::InternedId& out_node,
                              ui::InternedId& out_port) {
    if (path.kind() != bp2::PathKind::Port) return false;
    const bp2::Path parent = arena.parent(path);
    if (parent.kind() != bp2::PathKind::Node) return false;
    out_node = parent.segment();
    out_port = path.segment();
    return !out_node.empty() && !out_port.empty();
}

static std::string dedupe_name(const std::string& base,
                               std::unordered_set<std::string>& used) {
    if (used.find(base) == used.end()) {
        used.insert(base);
        return base;
    }
    for (int i = 2; i < 1000000; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (used.find(candidate) == used.end()) {
            used.insert(candidate);
            return candidate;
        }
    }
    return base + "_overflow";
}

static ui::InternedId next_unique_id(ui::StringInterner& interner,
                                     const std::unordered_set<ui::InternedId>& used,
                                     const std::string& prefix) {
    for (int i = 1; i < 1000000; ++i) {
        std::string candidate = prefix + std::to_string(i);
        ui::InternedId id = interner.lookup(candidate);
        if (id.empty() || used.find(id) == used.end()) {
            return interner.intern(candidate);
        }
    }
    return {};
}

static std::unordered_set<ui::InternedId> collect_used_node_ids(const bp2::Blueprint& bp) {
    std::unordered_set<ui::InternedId> out;
    out.reserve(bp.nodes().size() + bp.nested().size());
    for (const auto& n : bp.nodes()) out.insert(n.id);
    for (const auto& n : bp.nested()) out.insert(n.id);
    return out;
}

static std::unordered_set<ui::InternedId> collect_used_wire_ids(const bp2::Blueprint& bp) {
    std::unordered_set<ui::InternedId> out;
    out.reserve(bp.wires().size());
    for (const auto& w : bp.wires()) out.insert(w.id);
    return out;
}

static bool compare_external(const ExternalConnection& a, const ExternalConnection& b) {
    return std::make_tuple(a.original_wire_id.raw(),
                           a.external_node_id.raw(), a.external_port.raw(),
                           a.internal_node_id.raw(), a.internal_port.raw())
        < std::make_tuple(b.original_wire_id.raw(),
                          b.external_node_id.raw(), b.external_port.raw(),
                          b.internal_node_id.raw(), b.internal_port.raw());
}

static bp2::Blueprint::Nested clone_nested(const bp2::Blueprint::Nested& n) {
    bp2::Blueprint::Nested copy;
    copy.id = n.id;
    copy.blueprint_id = n.blueprint_id;
    copy.embedded = n.embedded;
    copy.iface = n.iface;
    copy.x = n.x;
    copy.y = n.y;
    if (n.inline_def) {
        copy.inline_def = std::make_unique<bp2::Blueprint>(*n.inline_def);
    }
    return copy;
}

static ui::InternedId make_iface_bridge_id(ui::StringInterner& interner,
                                           ui::InternedId nested_instance_id,
                                           const std::string& iface_name) {
    std::string id = std::string(interner.resolve(nested_instance_id));
    id += ":";
    id += iface_name;
    return interner.intern(id);
}

static std::optional<ExtractionPlan> analyze_selection(const bp2::Blueprint& bp,
                                                       const std::vector<ui::InternedId>& selected_ids,
                                                       ui::StringInterner& interner,
                                                       const bp2::PathArena& arena,
                                                       std::string* error_out) {
    ExtractionPlan plan;
    plan.selected_set.insert(selected_ids.begin(), selected_ids.end());
    if (plan.selected_set.size() < 2) {
        if (error_out) *error_out = "extract requires at least 2 selected nodes";
        return std::nullopt;
    }

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = -std::numeric_limits<float>::max();
    float max_y = -std::numeric_limits<float>::max();

    for (const auto& node : bp.nodes()) {
        if (plan.selected_set.find(node.id) == plan.selected_set.end()) continue;
        plan.internal_nodes.push_back(node);
        min_x = std::min(min_x, node.x);
        min_y = std::min(min_y, node.y);
        max_x = std::max(max_x, node.x + node.width.value_or(100.0f));
        max_y = std::max(max_y, node.y + node.height.value_or(64.0f));
    }

    if (plan.internal_nodes.size() < 2) {
        if (error_out) *error_out = "selected nodes not found in blueprint";
        return std::nullopt;
    }

    plan.min_x = min_x;
    plan.min_y = min_y;
    plan.max_x = max_x;
    plan.max_y = max_y;
    plan.center_x = (min_x + max_x) * 0.5f;
    plan.center_y = (min_y + max_y) * 0.5f;

    for (const auto& wire : bp.wires()) {
        ui::InternedId src_node, src_port, tgt_node, tgt_port;
        if (!path_to_node_port(wire.source, arena, src_node, src_port)
            || !path_to_node_port(wire.target, arena, tgt_node, tgt_port)) {
            if (error_out) *error_out = "wire endpoint path unresolved during extraction";
            return std::nullopt;
        }

        const bool src_selected = plan.selected_set.find(src_node) != plan.selected_set.end();
        const bool tgt_selected = plan.selected_set.find(tgt_node) != plan.selected_set.end();

        if (src_selected && tgt_selected) {
            plan.internal_wires.push_back(wire);
            continue;
        }
        if (!src_selected && !tgt_selected) {
            continue;
        }

        ExternalConnection ec;
        ec.original_wire_id = wire.id;
        ec.domain = wire.domain;
        if (!src_selected && tgt_selected) {
            ec.is_input = true;
            ec.external_node_id = src_node;
            ec.external_port = src_port;
            ec.internal_node_id = tgt_node;
            ec.internal_port = tgt_port;
            ec.iface_name = std::string(interner.resolve(tgt_port));
            plan.inputs.push_back(std::move(ec));
        } else {
            ec.is_input = false;
            ec.external_node_id = tgt_node;
            ec.external_port = tgt_port;
            ec.internal_node_id = src_node;
            ec.internal_port = src_port;
            ec.iface_name = std::string(interner.resolve(src_port));
            plan.outputs.push_back(std::move(ec));
        }
    }

    std::sort(plan.inputs.begin(), plan.inputs.end(), compare_external);
    std::sort(plan.outputs.begin(), plan.outputs.end(), compare_external);

    std::unordered_set<std::string> input_name_used;
    for (auto& ec : plan.inputs) {
        if (ec.iface_name.empty()) ec.iface_name = "in";
        ec.iface_name = dedupe_name(ec.iface_name, input_name_used);
    }
    std::unordered_set<std::string> output_name_used;
    for (auto& ec : plan.outputs) {
        if (ec.iface_name.empty()) ec.iface_name = "out";
        ec.iface_name = dedupe_name(ec.iface_name, output_name_used);
    }

    return plan;
}

static bp2::Blueprint build_inline_blueprint(const ExtractionPlan& plan,
                                             ui::StringInterner& interner,
                                             bp2::PathArena& arena,
                                             ui::InternedId blueprint_id) {
    bp2::Blueprint out;
    out = out.with_id(blueprint_id);
    out = out.with_display_name(std::string(interner.resolve(blueprint_id)));
    out = out.with_name(std::string(interner.resolve(blueprint_id)));

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    for (const auto& n : plan.internal_nodes) {
        min_x = std::min(min_x, n.x);
        min_y = std::min(min_y, n.y);
    }
    const float left_margin = 160.0f;

    float max_internal_right = 0.0f;
    for (auto node : plan.internal_nodes) {
        node.x = (node.x - min_x) + left_margin;
        node.y = (node.y - min_y);
        node.group_id.clear();
        max_internal_right = std::max(max_internal_right, node.x + node.width.value_or(100.0f));
        out = out.with_node(std::move(node));
    }

    std::vector<bp2::PortDescriptor> iface_ports;
    iface_ports.reserve(plan.inputs.size() + plan.outputs.size());
    for (const auto& ec : plan.inputs) {
        iface_ports.push_back({interner.intern(ec.iface_name), ec.domain, bp2::Direction::Input});
    }
    for (const auto& ec : plan.outputs) {
        iface_ports.push_back({interner.intern(ec.iface_name), ec.domain, bp2::Direction::Output});
    }
    out = out.with_interface(bp2::Interface(std::move(iface_ports)));

    std::unordered_set<ui::InternedId> used_node_ids = collect_used_node_ids(out);
    std::unordered_set<ui::InternedId> used_wire_ids = collect_used_wire_ids(out);

    std::unordered_map<std::string, ui::InternedId> input_bridge_ids;
    std::unordered_map<std::string, ui::InternedId> output_bridge_ids;

    float input_y = 0.0f;
    for (const auto& ec : plan.inputs) {
        ui::InternedId id = next_unique_id(interner, used_node_ids, "bp_in_");
        used_node_ids.insert(id);
        input_bridge_ids[ec.iface_name] = id;

        bp2::Blueprint::Node n;
        n.id = id;
        n.type = interner.intern("BlueprintInput");
        n.name = ec.iface_name;
        n.x = 0.0f;
        n.y = input_y;
        const PortType pt = port_type_for_domain(ec.domain);
        n.inputs.emplace_back(interner.intern("ext"), PortSide::Input, pt);
        n.outputs.emplace_back(interner.intern("port"), PortSide::Output, pt);
        out = out.with_node(std::move(n));
        input_y += 80.0f;
    }

    float output_y = 0.0f;
    for (const auto& ec : plan.outputs) {
        ui::InternedId id = next_unique_id(interner, used_node_ids, "bp_out_");
        used_node_ids.insert(id);
        output_bridge_ids[ec.iface_name] = id;

        bp2::Blueprint::Node n;
        n.id = id;
        n.type = interner.intern("BlueprintOutput");
        n.name = ec.iface_name;
        n.x = max_internal_right + 160.0f;
        n.y = output_y;
        const PortType pt = port_type_for_domain(ec.domain);
        n.inputs.emplace_back(interner.intern("port"), PortSide::Input, pt);
        n.outputs.emplace_back(interner.intern("ext"), PortSide::Output, pt);
        out = out.with_node(std::move(n));
        output_y += 80.0f;
    }

    for (const auto& w : plan.internal_wires) {
        ui::InternedId src_n, src_p, tgt_n, tgt_p;
        path_to_node_port(w.source, arena, src_n, src_p);
        path_to_node_port(w.target, arena, tgt_n, tgt_p);

        bp2::Blueprint::Wire nw = w;
        nw.source = arena.make_port(arena.make_node(arena.root(), src_n), src_p);
        nw.target = arena.make_port(arena.make_node(arena.root(), tgt_n), tgt_p);
        out = out.with_wire(std::move(nw));
    }

    for (const auto& ec : plan.inputs) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, "bp_bridge_in_wire_");
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        w.source = arena.make_port(
            arena.make_node(arena.root(), input_bridge_ids.at(ec.iface_name)),
            interner.intern("port"));
        w.target = arena.make_port(arena.make_node(arena.root(), ec.internal_node_id), ec.internal_port);
        out = out.with_wire(std::move(w));
    }
    for (const auto& ec : plan.outputs) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, "bp_bridge_out_wire_");
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        w.source = arena.make_port(arena.make_node(arena.root(), ec.internal_node_id), ec.internal_port);
        w.target = arena.make_port(
            arena.make_node(arena.root(), output_bridge_ids.at(ec.iface_name)),
            interner.intern("port"));
        out = out.with_wire(std::move(w));
    }

    return out;
}

} // namespace

std::optional<bp2::Blueprint> build_extracted_blueprint_atomic(
    const bp2::Blueprint& source,
    const std::vector<ui::InternedId>& selected_node_ids,
    const std::string& blueprint_name,
    const std::string& group_id,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    std::string* error_out) {
    if (!group_id.empty()) {
        if (error_out) *error_out = "extract MVP supports root group only";
        return std::nullopt;
    }
    if (blueprint_name.empty()) {
        if (error_out) *error_out = "extract blueprint name must be non-empty";
        return std::nullopt;
    }

    auto plan_opt = analyze_selection(source, selected_node_ids, interner, arena, error_out);
    if (!plan_opt) return std::nullopt;
    const ExtractionPlan& plan = *plan_opt;

    ui::InternedId blueprint_iid = interner.intern(blueprint_name);

    bp2::Blueprint out;
    out = out.with_id(source.id());
    out = out.with_display_name(source.display_name());
    out = out.with_name(source.name());
    out = out.with_interface(source.iface());
    out = out.with_viewport(source.pan_x(), source.pan_y(), source.zoom(), source.grid_step());

    std::unordered_set<ui::InternedId> used_node_ids = collect_used_node_ids(source);
    std::unordered_set<ui::InternedId> used_wire_ids = collect_used_wire_ids(source);
    ui::InternedId nested_instance_id = next_unique_id(interner, used_node_ids, "extract_inst_");
    if (nested_instance_id.empty()) {
        if (error_out) *error_out = "failed to allocate nested instance id";
        return std::nullopt;
    }
    used_node_ids.insert(nested_instance_id);
    const std::string nested_group_id = std::string(interner.resolve(nested_instance_id));

    for (const auto& nsrc : source.nodes()) {
        auto n = nsrc;
        if (plan.selected_set.find(n.id) != plan.selected_set.end()) {
            n.group_id = nested_group_id;
        }
        out = out.with_node(std::move(n));
    }

    for (const auto& w : source.wires()) {
        ui::InternedId src_node, src_port, tgt_node, tgt_port;
        if (!path_to_node_port(w.source, arena, src_node, src_port)
            || !path_to_node_port(w.target, arena, tgt_node, tgt_port)) {
            if (error_out) *error_out = "wire endpoint path unresolved during extraction";
            return std::nullopt;
        }
        const bool src_selected = plan.selected_set.find(src_node) != plan.selected_set.end();
        const bool tgt_selected = plan.selected_set.find(tgt_node) != plan.selected_set.end();
        if (src_selected != tgt_selected) continue;
        auto nw = w;
        nw.source = arena.make_port(arena.make_node(arena.root(), src_node), src_port);
        nw.target = arena.make_port(arena.make_node(arena.root(), tgt_node), tgt_port);
        out = out.with_wire(std::move(nw));
    }

    for (const auto& n : source.nested()) {
        out = out.with_nested(clone_nested(n));
    }

    bp2::Blueprint inline_bp = build_inline_blueprint(plan, interner, arena, blueprint_iid);

    bp2::Blueprint::Nested nested;
    nested.id = nested_instance_id;
    nested.blueprint_id = blueprint_iid;
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>(std::move(inline_bp));
    nested.iface = nested.inline_def->iface();
    nested.x = plan.center_x;
    nested.y = plan.center_y;
    out = out.with_nested(std::move(nested));

    bp2::Blueprint::Node collapsed;
    collapsed.id = nested_instance_id;
    collapsed.type = blueprint_iid;
    collapsed.name = blueprint_name;
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.blueprint_path = blueprint_name;
    collapsed.x = plan.center_x;
    collapsed.y = plan.center_y;
    collapsed.width = 160.0f;
    collapsed.height = 64.0f;
    for (const auto& pd : out.find_nested(nested_instance_id)->iface.ports()) {
        const PortType pt = port_type_for_domain(pd.domain);
        if (pd.direction == bp2::Direction::Input) {
            collapsed.inputs.emplace_back(pd.name, PortSide::Input, pt);
        } else if (pd.direction == bp2::Direction::Output) {
            collapsed.outputs.emplace_back(pd.name, PortSide::Output, pt);
        }
    }
    out = out.with_node(std::move(collapsed));

    std::unordered_map<std::string, ui::InternedId> input_bridge_ids;
    std::unordered_map<std::string, ui::InternedId> output_bridge_ids;

    std::unordered_set<std::string> input_iface_names;
    input_iface_names.reserve(plan.inputs.size());
    for (const auto& ec_in : plan.inputs) {
        input_iface_names.insert(ec_in.iface_name);
    }
    for (const auto& ec_out : plan.outputs) {
        if (input_iface_names.find(ec_out.iface_name) != input_iface_names.end()) {
            if (error_out) *error_out = "extract iface name collision between input/output";
            return std::nullopt;
        }
    }

    float input_y = plan.min_y;
    for (const auto& ec : plan.inputs) {
        ui::InternedId id = make_iface_bridge_id(interner, nested_instance_id, ec.iface_name);
        if (used_node_ids.find(id) != used_node_ids.end()) {
            if (error_out) *error_out = "extract bridge node id collision";
            return std::nullopt;
        }
        used_node_ids.insert(id);
        input_bridge_ids[ec.iface_name] = id;

        const PortType pt = port_type_for_domain(ec.domain);
        bp2::Blueprint::Node n;
        n.id = id;
        n.type = interner.intern("BlueprintInput");
        n.name = ec.iface_name;
        n.group_id = nested_group_id;
        n.x = plan.min_x - 160.0f;
        n.y = input_y;
        n.inputs.emplace_back(interner.intern("ext"), PortSide::Input, pt);
        n.outputs.emplace_back(interner.intern("port"), PortSide::Output, pt);
        out = out.with_node(std::move(n));
        input_y += 80.0f;
    }

    float output_y = plan.min_y;
    for (const auto& ec : plan.outputs) {
        ui::InternedId id = make_iface_bridge_id(interner, nested_instance_id, ec.iface_name);
        if (used_node_ids.find(id) != used_node_ids.end()) {
            if (error_out) *error_out = "extract bridge node id collision";
            return std::nullopt;
        }
        used_node_ids.insert(id);
        output_bridge_ids[ec.iface_name] = id;

        const PortType pt = port_type_for_domain(ec.domain);
        bp2::Blueprint::Node n;
        n.id = id;
        n.type = interner.intern("BlueprintOutput");
        n.name = ec.iface_name;
        n.group_id = nested_group_id;
        n.x = plan.max_x + 160.0f;
        n.y = output_y;
        n.inputs.emplace_back(interner.intern("port"), PortSide::Input, pt);
        n.outputs.emplace_back(interner.intern("ext"), PortSide::Output, pt);
        out = out.with_node(std::move(n));
        output_y += 80.0f;
    }

    for (const auto& ec : plan.inputs) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, "extract_wire_");
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        w.source = arena.make_port(
            arena.make_node(arena.root(), input_bridge_ids.at(ec.iface_name)),
            interner.intern("port"));
        w.target = arena.make_port(arena.make_node(arena.root(), ec.internal_node_id), ec.internal_port);
        out = out.with_wire(std::move(w));
    }
    for (const auto& ec : plan.outputs) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, "extract_wire_");
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        w.source = arena.make_port(arena.make_node(arena.root(), ec.internal_node_id), ec.internal_port);
        w.target = arena.make_port(
            arena.make_node(arena.root(), output_bridge_ids.at(ec.iface_name)),
            interner.intern("port"));
        out = out.with_wire(std::move(w));
    }

    for (const auto& ec : plan.inputs) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, "extract_wire_");
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        w.source = arena.make_port(arena.make_node(arena.root(), ec.external_node_id), ec.external_port);
        w.target = arena.make_port(arena.make_node(arena.root(), nested_instance_id), interner.intern(ec.iface_name));
        out = out.with_wire(std::move(w));
    }
    for (const auto& ec : plan.outputs) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, "extract_wire_");
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        w.source = arena.make_port(arena.make_node(arena.root(), nested_instance_id), interner.intern(ec.iface_name));
        w.target = arena.make_port(arena.make_node(arena.root(), ec.external_node_id), ec.external_port);
        out = out.with_wire(std::move(w));
    }

    std::string integrity_err;
    if (!validate_blueprint_integrity(out, interner, arena, &integrity_err)) {
        if (error_out) *error_out = integrity_err;
        return std::nullopt;
    }

    if (error_out) error_out->clear();
    return out;
}

} // namespace editor::commands
