#include "extract_blueprint_internal.h"

#include <algorithm>
#include <functional>

namespace editor::commands::extract_detail {

namespace {

// ========================================================================
// Local helpers
// ========================================================================

/// Build proxy iface ports from sorted external connections.
void append_proxy_ports(const std::vector<ExternalConnection>& conns,
                        bool is_input,
                        ui::StringInterner& interner,
                        std::vector<bp2::PortDescriptor>& proxy_ports_out) {
    const bp2::Direction dir = is_input ? bp2::Direction::Input : bp2::Direction::Output;
    for (const auto& ec : conns) {
        const PortType pt = resolve_port_type(ec);
        const ui::InternedId iface_iid = interner.intern(ec.iface_name);
        bp2::PortDescriptor pd;
        pd.name = iface_iid;
        pd.domain = ec.domain;
        pd.direction = dir;
        pd.port_type = pt;
        proxy_ports_out.push_back(std::move(pd));
    }
}

/// Build interface port descriptors from inputs + outputs.  Shared between
/// build_inline_blueprint (for the inline_def iface) and the proxy node.
std::vector<bp2::PortDescriptor> build_iface_ports(
    const std::vector<ExternalConnection>& inputs,
    const std::vector<ExternalConnection>& outputs,
    ui::StringInterner& interner) {
    std::vector<bp2::PortDescriptor> ports;
    ports.reserve(inputs.size() + outputs.size());
    for (const auto& ec : inputs) {
        bp2::PortDescriptor pd;
        pd.name = interner.intern(ec.iface_name);
        pd.domain = ec.domain;
        pd.direction = bp2::Direction::Input;
        pd.port_type = resolve_port_type(ec);
        ports.push_back(std::move(pd));
    }
    for (const auto& ec : outputs) {
        bp2::PortDescriptor pd;
        pd.name = interner.intern(ec.iface_name);
        pd.domain = ec.domain;
        pd.direction = bp2::Direction::Output;
        pd.port_type = resolve_port_type(ec);
        ports.push_back(std::move(pd));
    }
    return ports;
}

/// Create bridge nodes for both input and output sides, returning false on
/// error.  Reduces the repeated call + if-check scaffolding.
bool create_bridge_pair(
    bp2::Blueprint& out,
    const std::vector<ExternalConnection>& inputs,
    const std::vector<ExternalConnection>& outputs,
    const std::unordered_map<ui::InternedId, float>& node_center_y,
    float input_x,
    float output_x,
    float fallback_y_origin,
    const WindowScopeId& scope_id,
    const char* input_prefix,
    const char* output_prefix,
    const ui::InternedId* canonical_nested_instance_id,
    ui::StringInterner& interner,
    std::unordered_set<ui::InternedId>& used_node_ids,
    std::unordered_map<std::string, ui::InternedId>& input_bridge_ids,
    std::unordered_map<std::string, ui::InternedId>& output_bridge_ids,
    std::string* error_out) {
    BridgeSideBuildParams in_params{inputs, true, node_center_y,
                                    input_x, fallback_y_origin, scope_id,
                                    input_prefix, canonical_nested_instance_id};
    if (!create_bridge_nodes_for_side(out, in_params, interner,
                                      used_node_ids, input_bridge_ids, error_out)) {
        return false;
    }
    BridgeSideBuildParams out_params{outputs, false, node_center_y,
                                     output_x, fallback_y_origin, scope_id,
                                     output_prefix, canonical_nested_instance_id};
    if (!create_bridge_nodes_for_side(out, out_params, interner,
                                      used_node_ids, output_bridge_ids, error_out)) {
        return false;
    }
    return true;
}

/// Create external reconnection wires (from/to the collapsed nested instance
/// node) for a list of external connections.  For inputs the direction is
/// external→nested; for outputs it is nested→external.
void create_external_reconnection_wires(
    bp2::Blueprint& out,
    const std::vector<ExternalConnection>& conns,
    bool is_input,
    ui::InternedId nested_instance_id,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    std::unordered_set<ui::InternedId>& used_wire_ids) {
    for (const auto& ec : conns) {
        bp2::Blueprint::Wire w;
        w.id = next_unique_id(interner, used_wire_ids, "extract_wire_");
        used_wire_ids.insert(w.id);
        w.domain = ec.domain;
        const auto nested_path = arena.make_port(
            arena.make_node(arena.root(), nested_instance_id),
            interner.intern(ec.iface_name));
        const auto external_path = arena.make_port(
            arena.make_node(arena.root(), ec.external_node_id),
            ec.external_port);
        if (is_input) {
            w.source = external_path;
            w.target = nested_path;
        } else {
            w.source = nested_path;
            w.target = external_path;
        }
        out = out.with_wire(std::move(w));
    }
}

// ========================================================================

bool append_selected_embedded_nested_for_inline(
    bp2::Blueprint& inline_bp,
    const bp2::Blueprint& source,
    const std::unordered_set<ui::InternedId>& selected_set,
    bool allow_nonembedded_descendant_refs,
    std::string* error_out) {
    std::unordered_map<ui::InternedId, const bp2::Blueprint::Nested*> embedded_by_blueprint_id;
    for (const auto& n : source.nested()) {
        if (!n.is_embedded() || !n.inline_def()) {
            continue;
        }
        auto it = embedded_by_blueprint_id.find(n.blueprint_id());
        if (it == embedded_by_blueprint_id.end() || n.id.raw() < it->second->id.raw()) {
            embedded_by_blueprint_id[n.blueprint_id()] = &n;
        }
    }

    std::function<bool(bp2::Blueprint::Nested&)> remap_descendants;
    remap_descendants = [&](bp2::Blueprint::Nested& owner) -> bool {
        if (!owner.inline_def()) {
            return true;
        }

        std::vector<bp2::Blueprint::Nested> remapped;
        remapped.reserve(owner.inline_def()->nested().size());
        for (const auto& child_src : owner.inline_def()->nested()) {
            bp2::Blueprint::Nested child = child_src;
            if (!child.is_embedded()) {
                auto it = embedded_by_blueprint_id.find(child.blueprint_id());
                if (it != embedded_by_blueprint_id.end()) {
                    const bp2::Blueprint::Nested* resolved = it->second;
                    // Convert reference to embedded by creating new Embedded content
                    child.convert_to_embedded(
                        child.blueprint_id(),
                        resolved->inline_def()
                            ? std::make_unique<bp2::Blueprint>(*resolved->inline_def())
                            : std::make_unique<bp2::Blueprint>()
                    );
                } else if (!allow_nonembedded_descendant_refs) {
                    return set_error(error_out, "selected embedded nested contains non-embedded descendant references");
                }
            }

            if (!remap_descendants(child)) {
                return false;
            }
            remapped.push_back(std::move(child));
        }

        bp2::Blueprint rebuilt = *owner.inline_def();
        for (const auto& existing : owner.inline_def()->nested()) {
            rebuilt = rebuilt.without_nested(existing.id);
        }
        for (auto& child : remapped) {
            rebuilt = rebuilt.with_nested(std::move(child));
        }
        owner.set_inline_def(std::make_unique<bp2::Blueprint>(std::move(rebuilt)));
        return true;
    };

    std::vector<const bp2::Blueprint::Nested*> selected_nested;
    selected_nested.reserve(source.nested().size());
    for (const auto& n : source.nested()) {
        if (selected_set.find(n.id) == selected_set.end()) {
            continue;
        }
        selected_nested.push_back(&n);
    }

    std::sort(selected_nested.begin(), selected_nested.end(), [](const auto* a, const auto* b) {
        return a->id.raw() < b->id.raw();
    });

    for (const auto* n : selected_nested) {
        if (!n->is_embedded()) {
            return set_error(error_out, "selected non-embedded nested instance cannot be inlined");
        }
        if (!n->inline_def()) {
            return set_error(error_out, "selected embedded nested instance missing inline_def");
        }
        if (inline_bp.find_nested(n->id) != nullptr) {
            return set_error(error_out, "inline merge nested id collision");
        }
        bp2::Blueprint::Nested copy = clone_nested(*n);
        if (!remap_descendants(copy)) {
            return false;
        }
        inline_bp = inline_bp.with_nested(std::move(copy));
    }
    return true;
}

std::optional<bp2::Blueprint> build_inline_blueprint(
    const ExtractionPlan& plan,
    const bp2::Blueprint& source,
    bool allow_nonembedded_descendant_refs,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    ui::InternedId blueprint_id,
    std::string* error_out) {
    bp2::Blueprint out;
    out = out.with_id(blueprint_id);
    out = out.with_display_name(std::string(interner.resolve(blueprint_id)));
    out = out.with_name(std::string(interner.resolve(blueprint_id)));

    const float min_x = plan.min_x;
    const float min_y = plan.min_y;
    const float left_margin = kBridgeMarginX;

    std::vector<bp2::Blueprint::Node> translated_nodes;
    translated_nodes.reserve(plan.internal_nodes.size());
    float max_internal_right = 0.0f;
    for (auto node : plan.internal_nodes) {
        node.layout.x = (node.layout.x - min_x) + left_margin;
        node.layout.y = (node.layout.y - min_y);
        node.layout.layout_group.clear();
        max_internal_right = std::max(max_internal_right, node.layout.x + node.layout.width.value_or(kDefaultNodeWidth));
        translated_nodes.push_back(node);
        out = out.with_node(std::move(node));
    }

    out = out.with_interface(bp2::Interface(build_iface_ports(plan.inputs, plan.outputs, interner)));

    std::unordered_set<ui::InternedId> used_node_ids = collect_used_node_ids(out);
    std::unordered_set<ui::InternedId> used_wire_ids = collect_used_wire_ids(out);

    std::unordered_map<std::string, ui::InternedId> input_bridge_ids;
    std::unordered_map<std::string, ui::InternedId> output_bridge_ids;

    const auto node_center_y = build_node_center_y_map(translated_nodes);
    if (!create_bridge_pair(out,
                            plan.inputs, plan.outputs,
                            node_center_y,
                            0.0f,
                            max_internal_right + kBridgeMarginX,
                            0.0f,
                            WindowScopeId::root(),
                            "bp_in_", "bp_out_",
                            nullptr,
                            interner, used_node_ids,
                            input_bridge_ids, output_bridge_ids,
                            error_out)) {
        return std::nullopt;
    }

    for (const auto& w : plan.internal_wires) {
        ui::InternedId src_n;
        ui::InternedId src_p;
        ui::InternedId tgt_n;
        ui::InternedId tgt_p;
        path_to_node_port(w.source, arena, src_n, src_p);
        path_to_node_port(w.target, arena, tgt_n, tgt_p);

        bp2::Blueprint::Wire nw = w;
        nw.source = arena.make_port(arena.make_node(arena.root(), src_n), src_p);
        nw.target = arena.make_port(arena.make_node(arena.root(), tgt_n), tgt_p);
        out = out.with_wire(std::move(nw));
    }

    append_bridge_to_internal_wires(out, plan.inputs, true, input_bridge_ids,
                                    "bp_bridge_in_wire_", interner, arena, used_wire_ids);
    append_bridge_to_internal_wires(out, plan.outputs, false, output_bridge_ids,
                                    "bp_bridge_out_wire_", interner, arena, used_wire_ids);

    if (!append_selected_embedded_nested_for_inline(
            out,
            source,
            plan.selected_set,
            allow_nonembedded_descendant_refs,
            error_out)) {
        return std::nullopt;
    }

    return out;
}

} // namespace

std::optional<bp2::Blueprint> build_parent_blueprint_from_plan(
    const bp2::Blueprint& source,
    const ExtractionPlan& plan,
    ui::InternedId blueprint_iid,
    const std::string& blueprint_name,
    const WindowScopeId& scope_id,
    bool allow_nonembedded_descendant_refs,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    std::string* error_out) {
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
        set_error(error_out, "failed to allocate nested instance id");
        return std::nullopt;
    }
    used_node_ids.insert(nested_instance_id);
    const std::string nested_scope_key = std::string(interner.resolve(nested_instance_id));
    const WindowScopeId nested_scope_id = WindowScopeId::embedded(nested_scope_key);

    for (const auto& nsrc : source.nodes()) {
        auto n = nsrc;
        if (plan.selected_set.find(n.semantic.id) != plan.selected_set.end()) {
            n.layout.layout_group = nested_scope_key;
        }
        out = out.with_node(std::move(n));
    }

    for (const auto& w : source.wires()) {
        ui::InternedId src_node;
        ui::InternedId src_port;
        ui::InternedId tgt_node;
        ui::InternedId tgt_port;
        if (!path_to_node_port(w.source, arena, src_node, src_port)
            || !path_to_node_port(w.target, arena, tgt_node, tgt_port)) {
            set_error(error_out, "wire endpoint path unresolved during extraction");
            return std::nullopt;
        }
        const bool src_selected = plan.selected_set.find(src_node) != plan.selected_set.end();
        const bool tgt_selected = plan.selected_set.find(tgt_node) != plan.selected_set.end();
        if (src_selected != tgt_selected) {
            continue;
        }
        auto nw = w;
        nw.source = arena.make_port(arena.make_node(arena.root(), src_node), src_port);
        nw.target = arena.make_port(arena.make_node(arena.root(), tgt_node), tgt_port);
        out = out.with_wire(std::move(nw));
    }

    for (const auto& n : source.nested()) {
        out = out.with_nested(clone_nested(n));
    }

    auto inline_bp_opt = build_inline_blueprint(
        plan, source, allow_nonembedded_descendant_refs,
        interner, arena, blueprint_iid, error_out);
    if (!inline_bp_opt) {
        return std::nullopt;
    }
    bp2::Blueprint inline_bp = std::move(*inline_bp_opt);

    auto nested = bp2::Blueprint::Nested::make_embedded(
        nested_instance_id, blueprint_iid,
        std::make_unique<bp2::Blueprint>(std::move(inline_bp)),
        plan.center_x, plan.center_y);
    out = out.with_nested(std::move(nested));

    // -- Build collapsed proxy node ------------------------------------------
    bp2::Blueprint::Node collapsed;
    collapsed.semantic.id = nested_instance_id;
    collapsed.semantic.type = blueprint_iid;
    collapsed.view.name = blueprint_name;
    collapsed.view.expandable = true;
    collapsed.layout.collapsed = true;
    collapsed.view.blueprint_path = blueprint_name;
    collapsed.layout.layout_group = scope_id.sim_scope_prefix();
    collapsed.layout.x = plan.center_x;
    collapsed.layout.y = plan.center_y;
    collapsed.layout.width = 160.0f;
    collapsed.layout.height = 64.0f;
    std::vector<ExternalConnection> sorted_inputs = plan.inputs;
    std::vector<ExternalConnection> sorted_outputs = plan.outputs;
    std::sort(sorted_inputs.begin(), sorted_inputs.end(), compare_external);
    std::sort(sorted_outputs.begin(), sorted_outputs.end(), compare_external);
    std::vector<bp2::PortDescriptor> proxy_ports;
    proxy_ports.reserve(sorted_inputs.size() + sorted_outputs.size());
    append_proxy_ports(sorted_inputs, true, interner, proxy_ports);
    append_proxy_ports(sorted_outputs, false, interner, proxy_ports);
    collapsed.semantic.iface = bp2::Interface(std::move(proxy_ports));
    out = out.with_node(std::move(collapsed));

    // -- Iface collision check -----------------------------------------------
    std::unordered_set<std::string> input_iface_names;
    input_iface_names.reserve(plan.inputs.size());
    for (const auto& ec_in : plan.inputs) {
        input_iface_names.insert(ec_in.iface_name);
    }
    for (const auto& ec_out : plan.outputs) {
        if (input_iface_names.find(ec_out.iface_name) != input_iface_names.end()) {
            set_error(error_out, "extract iface name collision between input/output");
            return std::nullopt;
        }
    }

    // -- Bridge nodes (parent level) -----------------------------------------
    std::unordered_map<std::string, ui::InternedId> input_bridge_ids;
    std::unordered_map<std::string, ui::InternedId> output_bridge_ids;
    const auto node_center_y = build_node_center_y_map(plan.internal_nodes);
    if (!create_bridge_pair(out,
                            plan.inputs, plan.outputs,
                            node_center_y,
                            plan.min_x - 160.0f,
                            plan.max_x + 160.0f,
                            plan.min_y,
                            nested_scope_id,
                            "", "",
                            &nested_instance_id,
                            interner, used_node_ids,
                            input_bridge_ids, output_bridge_ids,
                            error_out)) {
        return std::nullopt;
    }

    append_bridge_to_internal_wires(out, plan.inputs, true, input_bridge_ids,
                                    "extract_wire_", interner, arena, used_wire_ids);
    append_bridge_to_internal_wires(out, plan.outputs, false, output_bridge_ids,
                                    "extract_wire_", interner, arena, used_wire_ids);

    // -- External reconnection wires -----------------------------------------
    create_external_reconnection_wires(out, plan.inputs, true,
                                       nested_instance_id, interner, arena, used_wire_ids);
    create_external_reconnection_wires(out, plan.outputs, false,
                                       nested_instance_id, interner, arena, used_wire_ids);

    return out;
}

} // namespace editor::commands::extract_detail
