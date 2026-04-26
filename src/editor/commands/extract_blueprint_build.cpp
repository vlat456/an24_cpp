#include "extract_blueprint_internal.h"

#include "blueprint_v2/editor_model/editor_model.h"

#include <algorithm>

namespace editor::commands::extract_detail {

namespace {

// ========================================================================
// Local helpers
// ========================================================================

// ========================================================================

bool append_selected_embedded_nested_for_inline(
    bp2::Blueprint& inline_bp,
    const bp2::Blueprint& source,
    const std::unordered_set<core::InternedId>& selected_set,
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
    core::StringInterner& interner,
    bp2::PathArena& arena,
    core::InternedId blueprint_id,
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

    const auto boundary = synthesize_extracted_boundary(
        plan, core::InternedId{}, translated_nodes, interner, error_out);
    if (!boundary) {
        return std::nullopt;
    }

    out = out.with_interface(boundary->child_interface);

    for (auto node : boundary->child_bridge_nodes) {
        out = out.with_node(std::move(node));
    }

    for (const auto& w : plan.internal_wires) {
        out = out.with_wire(w);
    }

    for (auto wire : boundary->child_bridge_wires) {
        out = out.with_wire(std::move(wire));
    }

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
    core::InternedId blueprint_iid,
    const std::string& blueprint_name,
    const WindowScopeId& scope_id,
    bool allow_nonembedded_descendant_refs,
    core::StringInterner& interner,
    bp2::PathArena& arena,
    std::string* error_out) {
    (void)scope_id;

    bp2::Blueprint out;
    out = out.with_id(source.id());
    out = out.with_name(source.name());
    out = out.with_interface(source.iface());

    std::unordered_set<core::InternedId> used_node_ids = collect_used_node_ids(source);
    core::InternedId nested_instance_id = next_unique_id(interner, used_node_ids, "extract_inst_");
    if (nested_instance_id.empty()) {
        set_error(error_out, "failed to allocate nested instance id");
        return std::nullopt;
    }
    used_node_ids.insert(nested_instance_id);

      for (const auto& nsrc : source.nodes()) {
          if (plan.selected_set.find(nsrc.semantic.id) != plan.selected_set.end()) {
              continue;
         }
         out = out.with_node(nsrc);
     }

    for (const auto& w : source.wires()) {
        core::InternedId src_node = w.source.node;
        core::InternedId tgt_node = w.target.node;
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

    const auto boundary = synthesize_extracted_boundary(
        plan, nested_instance_id, plan.internal_nodes, interner, error_out);
    if (!boundary) {
        return std::nullopt;
    }

    for (auto wire : boundary->parent_reconnection_wires) {
        out = out.with_wire(std::move(wire));
    }

    return out;
}

} // namespace editor::commands::extract_detail
