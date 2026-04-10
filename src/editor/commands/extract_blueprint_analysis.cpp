#include "extract_blueprint_internal.h"
#include "blueprint_v2/editor_model/editor_model.h"

#include <algorithm>
#include <limits>

namespace editor::commands::extract_detail {

namespace {

bool is_bridge_node_type(ui::StringInterner& interner, ui::InternedId type) {
    return type == interner.intern("BlueprintInput")
        || type == interner.intern("BlueprintOutput");
}

bool node_allowed_in_scope(const bp2::Blueprint::Node& node, const WindowScopeId& scope_id) {
    if (scope_id.is_root()) {
        return true;
    }
    if (!scope_id.is_embedded()) {
        return false;
    }
    return node.semantic.id != ui::InternedId{};
}

std::string default_iface_name_for(const ExternalConnection& ec, ui::StringInterner& interner) {
    const auto* port_name = interner.resolve(ec.internal_port).data();
    (void)port_name;
    return std::string(interner.resolve(ec.internal_port));
}

bool find_embedded_provider_blueprint(const bp2::Blueprint& bp,
                                      ui::InternedId blueprint_id,
                                      const bp2::Blueprint** out_provider) {
    const bp2::Blueprint* best = nullptr;
    ui::InternedId best_node_id;

    for (const auto& node : bp.nodes()) {
        if (!node.has_embedded_blueprint() || !node.source || !node.source->inline_def()) {
            continue;
        }
        if (node.source->blueprint_id() != blueprint_id) {
            continue;
        }
        if (!best || node.semantic.id.raw() < best_node_id.raw()) {
            best = node.source->inline_def();
            best_node_id = node.semantic.id;
        }
    }

    if (!best) {
        return false;
    }
    if (out_provider) {
        *out_provider = best;
    }
    return true;
}

bool inline_nonembedded_descendants(bp2::Blueprint& bp,
                                    const bp2::Blueprint& source,
                                    bool allow_nonembedded_descendant_refs,
                                    DescendantRemapStats* stats,
                                    std::string* error_out,
                                    ui::StringInterner& interner) {
    for (const auto& node_src : bp.nodes()) {
        if (!node_src.has_embedded_blueprint() || !node_src.source || !node_src.source->inline_def()) {
            continue;
        }

        bp2::Blueprint::Node updated_node = node_src;
        bp2::Blueprint inline_bp = *node_src.source->inline_def();
        bool changed = false;

        for (const auto& child_src : inline_bp.nodes()) {
            if (!child_src.has_referenced_blueprint() || !child_src.source) {
                continue;
            }

            if (!allow_nonembedded_descendant_refs) {
                return set_error(error_out, "selected embedded blueprint contains non-embedded descendant references");
            }

            const bp2::Blueprint* provider = nullptr;
            if (find_embedded_provider_blueprint(source, child_src.source->blueprint_id(), &provider)) {
                bp2::Blueprint::Node remapped = child_src;
                remapped.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
                    child_src.source->blueprint_id(),
                    std::make_unique<bp2::Blueprint>(*provider));
                remapped.semantic.iface = remapped.source->resolved_iface();
                inline_bp = bp2::replace_node_preserve_order(inline_bp, std::move(remapped));
                changed = true;
                if (stats) {
                    ++stats->remapped;
                }
            } else if (stats) {
                ++stats->passthrough;
            }
        }

        if (changed) {
            updated_node.source->set_inline_def(std::make_unique<bp2::Blueprint>(std::move(inline_bp)));
            bp = bp2::replace_node_preserve_order(bp, std::move(updated_node));
        }
    }

    return true;
}

} // namespace

bool contains_nonembedded_descendant_nested(const bp2::Blueprint& bp) {
    for (const auto& node : bp.nodes()) {
        if (!node.is_blueprint_instance() || !node.source) {
            continue;
        }
        if (node.source->is_reference()) {
            return true;
        }
        const auto* inline_bp = node.source->inline_def();
        if (inline_bp && contains_nonembedded_descendant_nested(*inline_bp)) {
            return true;
        }
    }
    return false;
}

bool validate_selected_embedded_nested_merge_safety(const bp2::Blueprint& source,
                                                    const std::unordered_set<ui::InternedId>& selected_set,
                                                    std::string* error_out) {
    (void)source;
    (void)selected_set;
    (void)error_out;
    return true;
}

bool validate_blueprint_name_for_extract(const bp2::Blueprint& source,
                                         const std::string& blueprint_name,
                                         ui::StringInterner& interner,
                                         ui::InternedId* blueprint_iid_out,
                                         std::string* error_out) {
    if (blueprint_name.empty()) {
        return set_error(error_out, "extracted blueprint name must be non-empty");
    }

    ui::InternedId blueprint_iid = interner.intern(blueprint_name);
    for (const auto& node : source.nodes()) {
        if (!node.is_blueprint_instance() || !node.source) {
            continue;
        }
        if (node.source->blueprint_id() == blueprint_iid) {
            return set_error(error_out, "blueprint with this name already exists");
        }
    }

    if (blueprint_iid_out) {
        *blueprint_iid_out = blueprint_iid;
    }
    return true;
}

DescendantRemapStats collect_descendant_remap_stats(
    const bp2::Blueprint& bp,
    const std::unordered_set<ui::InternedId>& selected_set,
    bool allow_nonembedded) {
    DescendantRemapStats stats;
    for (const auto& node : bp.nodes()) {
        if (selected_set.find(node.semantic.id) == selected_set.end()) {
            continue;
        }
        if (!node.has_embedded_blueprint() || !node.source || !node.source->inline_def()) {
            continue;
        }

        const auto* inline_bp = node.source->inline_def();
        if (!contains_nonembedded_descendant_nested(*inline_bp)) {
            continue;
        }

        for (const auto& child : inline_bp->nodes()) {
            if (!child.has_referenced_blueprint() || !child.source) {
                continue;
            }
            if (!allow_nonembedded) {
                continue;
            }
            const bp2::Blueprint* provider = nullptr;
            if (find_embedded_provider_blueprint(bp, child.source->blueprint_id(), &provider)) {
                ++stats.remapped;
            } else {
                ++stats.passthrough;
            }
        }
    }
    return stats;
}

std::optional<ExtractionPlan> analyze_selection(const bp2::Blueprint& bp,
                                                 const std::vector<ui::InternedId>& selected_ids,
                                                 const WindowScopeId& scope_id,
                                                  bool allow_nonembedded,
                                                  ui::StringInterner& interner,
                                                  const bp2::PathArena& arena,
                                                  std::string* error_out) {
    if (selected_ids.size() < 2) {
        return set_error(error_out, "extract selection must include at least 2 nodes"), std::nullopt;
    }

    std::unordered_set<ui::InternedId> selected_set(selected_ids.begin(), selected_ids.end());
    if (selected_set.size() < 2) {
        return set_error(error_out, "extract selection must include at least 2 nodes"), std::nullopt;
    }

    ExtractionPlan plan;
    plan.selected_set = selected_set;
    plan.min_x = std::numeric_limits<float>::max();
    plan.min_y = std::numeric_limits<float>::max();
    plan.max_x = std::numeric_limits<float>::lowest();
    plan.max_y = std::numeric_limits<float>::lowest();

    for (ui::InternedId node_id : selected_ids) {
        const auto* node = bp.find_node(node_id);
        if (!node) {
            return set_error(error_out, "selected node not found"), std::nullopt;
        }
        if (!node_allowed_in_scope(*node, scope_id)) {
            return set_error(error_out, "selected nodes must belong to the active group"), std::nullopt;
        }
        if (is_bridge_node_type(interner, node->semantic.type)) {
            return set_error(error_out, "cannot extract BlueprintInput/BlueprintOutput bridge nodes"), std::nullopt;
        }
        if (node->has_referenced_blueprint()) {
            return set_error(error_out, "cannot extract non-embedded nested instances"), std::nullopt;
        }
        if (node->has_embedded_blueprint() && node->source && node->source->inline_def()
            && contains_nonembedded_descendant_nested(*node->source->inline_def())
            && !allow_nonembedded) {
            return set_error(error_out, "selected embedded blueprint contains non-embedded descendant references"), std::nullopt;
        }

        plan.internal_nodes.push_back(*node);
        const float width = node->layout.width.value_or(kDefaultNodeWidth);
        const float height = node->layout.height.value_or(kDefaultNodeHeight);
        plan.min_x = std::min(plan.min_x, node->layout.x);
        plan.min_y = std::min(plan.min_y, node->layout.y);
        plan.max_x = std::max(plan.max_x, node->layout.x + width);
        plan.max_y = std::max(plan.max_y, node->layout.y + height);
    }

    if (!validate_selected_embedded_nested_merge_safety(bp, selected_set, error_out)) {
        return std::nullopt;
    }

    for (const auto& wire : bp.wires()) {
        ui::InternedId src_node;
        ui::InternedId src_port;
        ui::InternedId tgt_node;
        ui::InternedId tgt_port;
        if (!path_to_node_port(wire.source, arena, src_node, src_port)
            || !path_to_node_port(wire.target, arena, tgt_node, tgt_port)) {
            continue;
        }

        const bool src_selected = selected_set.find(src_node) != selected_set.end();
        const bool tgt_selected = selected_set.find(tgt_node) != selected_set.end();
        if (src_selected && tgt_selected) {
            plan.internal_wires.push_back(wire);
            continue;
        }
        if (src_selected == tgt_selected) {
            continue;
        }

        ExternalConnection ec;
        ec.original_wire_id = wire.id;
        ec.domain = wire.domain;
        ec.is_input = !src_selected && tgt_selected;
        if (ec.is_input) {
            ec.external_node_id = src_node;
            ec.external_port = src_port;
            ec.internal_node_id = tgt_node;
            ec.internal_port = tgt_port;
        } else {
            ec.external_node_id = tgt_node;
            ec.external_port = tgt_port;
            ec.internal_node_id = src_node;
            ec.internal_port = src_port;
        }

        const auto* internal_node = bp.find_node(ec.internal_node_id);
        ec.port_type = find_port_type(bp, internal_node, ec.internal_port);
        ec.iface_name = default_iface_name_for(ec, interner);
        if (ec.is_input) {
            plan.inputs.push_back(std::move(ec));
        } else {
            plan.outputs.push_back(std::move(ec));
        }
    }

    std::sort(plan.inputs.begin(), plan.inputs.end(), compare_external);
    std::sort(plan.outputs.begin(), plan.outputs.end(), compare_external);
    dedupe_iface_names(plan.inputs, "in");
    dedupe_iface_names(plan.outputs, "out");

    plan.center_x = (plan.min_x + plan.max_x) * 0.5f;
    plan.center_y = (plan.min_y + plan.max_y) * 0.5f;
    if (plan.internal_nodes.empty()) {
        return set_error(error_out, "extract selection is empty"), std::nullopt;
    }
    return plan;
}

} // namespace editor::commands::extract_detail
