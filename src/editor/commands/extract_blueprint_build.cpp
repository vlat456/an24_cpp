#include "extract_blueprint_internal.h"

#include "blueprint_v2/editor_model/editor_model.h"

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
        const bp2::WireEndpoint nested_ep{nested_instance_id, interner.intern(ec.iface_name)};
        const bp2::WireEndpoint external_ep{ec.external_node_id, ec.external_port};
        if (is_input) {
            w.source = external_ep;
            w.target = nested_ep;
        } else {
            w.source = nested_ep;
            w.target = external_ep;
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
    for (const auto& source_node : source.nodes()) {
        if (selected_set.find(source_node.semantic.id) == selected_set.end()) {
            continue;
        }
        if (!source_node.has_embedded_blueprint()) {
            continue;
        }

        bp2::Blueprint::Node updated = source_node;
        bp2::Blueprint child_bp = *source_node.blueprint_instance().source.inline_def();

        for (const auto& child_node : child_bp.nodes()) {
            if (!child_node.has_referenced_blueprint()) {
                continue;
            }
            if (!allow_nonembedded_descendant_refs) {
                return set_error(error_out, "selected embedded blueprint contains non-embedded descendant references");
            }

            const bp2::Blueprint* provider = nullptr;
            for (const auto& provider_node : source.nodes()) {
                if (!provider_node.has_embedded_blueprint()) {
                    continue;
                }
                if (provider_node.blueprint_instance().source.blueprint_id() != child_node.blueprint_instance().source.blueprint_id()) {
                    continue;
                }
                if (!provider || provider_node.semantic.id.raw() < provider->id().raw()) {
                    provider = provider_node.blueprint_instance().source.inline_def();
                }
            }

            if (!provider) {
                continue;
            }

            bp2::Blueprint::Node remapped = child_node;
            remapped.content = bp2::Blueprint::Node::BlueprintInstanceData{
                bp2::Blueprint::Node::BlueprintSource::make_embedded(
                    std::make_unique<bp2::Blueprint>(*provider))
            };
            child_bp = bp2::replace_node_preserve_order(child_bp, std::move(remapped));
        }

        updated.blueprint_instance().source.set_inline_def(std::make_unique<bp2::Blueprint>(std::move(child_bp)));
        inline_bp = bp2::replace_node_preserve_order(inline_bp, std::move(updated));
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
        out = out.with_wire(w);
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
    out = out.with_name(source.name());
    out = out.with_interface(source.iface());

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
         if (plan.selected_set.find(nsrc.semantic.id) != plan.selected_set.end()) {
             continue;
         }
         out = out.with_node(nsrc);
     }

    for (const auto& w : source.wires()) {
        ui::InternedId src_node = w.source.node;
        ui::InternedId tgt_node = w.target.node;
        if (src_node.empty() || tgt_node.empty()) {
            set_error(error_out, "wire endpoint path unresolved during extraction");
            return std::nullopt;
        }
        const bool src_selected = plan.selected_set.find(src_node) != plan.selected_set.end();
        const bool tgt_selected = plan.selected_set.find(tgt_node) != plan.selected_set.end();
        // Skip boundary wires (one endpoint selected) — they get reconnected
        // through the collapsed instance node.
        // Skip internal wires (both endpoints selected) — they live inside
        // the extracted inline blueprint.
        if (src_selected || tgt_selected) {
            continue;
        }
        out = out.with_wire(w);
    }

    // Skip copying nested blueprints (TODO BLUEPRINT_83 migration)

    auto inline_bp_opt = build_inline_blueprint(
        plan, source, allow_nonembedded_descendant_refs,
        interner, arena, blueprint_iid, error_out);
    if (!inline_bp_opt) {
        return std::nullopt;
    }
    bp2::Blueprint inline_bp = std::move(*inline_bp_opt);

    // -- Build blueprint-instance node with embedded source ---
    bp2::Blueprint::Node collapsed;
    collapsed.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(std::move(inline_bp)))
    };
    collapsed.semantic.id = nested_instance_id;
    collapsed.semantic.type = blueprint_iid;
    collapsed.view.name = blueprint_name;
    collapsed.layout.collapsed = true;
    collapsed.layout.x = plan.center_x;
    collapsed.layout.y = plan.center_y;
    collapsed.layout.width = 160.0f;
    collapsed.layout.height = 64.0f;
    
    std::vector<ExternalConnection> sorted_inputs = plan.inputs;
    std::vector<ExternalConnection> sorted_outputs = plan.outputs;
    std::sort(sorted_inputs.begin(), sorted_inputs.end(), compare_external);
    std::sort(sorted_outputs.begin(), sorted_outputs.end(), compare_external);
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

    // Extend parent interface with ports for the new bridges.
    auto parent_ports = out.iface().ports();
    for (const auto& ec : plan.inputs) {
        auto name_iid = interner.intern(ec.iface_name);
        if (!out.iface().has(name_iid)) {
            parent_ports.push_back(bp2::PortDescriptor{
                name_iid, ec.domain, bp2::Direction::Input, resolve_port_type(ec)});
        }
    }
    for (const auto& ec : plan.outputs) {
        auto name_iid = interner.intern(ec.iface_name);
        if (!out.iface().has(name_iid)) {
            parent_ports.push_back(bp2::PortDescriptor{
                name_iid, ec.domain, bp2::Direction::Output, resolve_port_type(ec)});
        }
    }
    out = out.with_interface(bp2::Interface(std::move(parent_ports)));

    // -- External reconnection wires -----------------------------------------
    create_external_reconnection_wires(out, plan.inputs, true,
                                       nested_instance_id, interner, arena, used_wire_ids);
    create_external_reconnection_wires(out, plan.outputs, false,
                                       nested_instance_id, interner, arena, used_wire_ids);

    return out;
}

} // namespace editor::commands::extract_detail
