#include "extract_blueprint_internal.h"

#include <algorithm>
#include <functional>
#include <limits>

namespace editor::commands::extract_detail {

namespace {

bool contains_nonembedded_descendant_nested(const bp2::Blueprint& bp) {
    for (const auto& n : bp.nested()) {
        if (!n.is_embedded()) {
            return true;
        }
        if (auto* def = n.inline_def(); def && contains_nonembedded_descendant_nested(*def)) {
            return true;
        }
    }
    return false;
}

bool validate_selected_embedded_nested_merge_safety(const bp2::Blueprint& source,
                                                    const std::unordered_set<ui::InternedId>& selected_set,
                                                    bool allow_nonembedded_descendant_refs,
                                                    std::string* error_out) {
    if (allow_nonembedded_descendant_refs) {
        return true;
    }
    for (const auto& n : source.nested()) {
        if (selected_set.find(n.id) == selected_set.end()) {
            continue;
        }
        if (!n.is_embedded() || !n.inline_def()) {
            continue;
        }
        if (contains_nonembedded_descendant_nested(*n.inline_def())) {
            return set_error(error_out, "selected embedded nested contains non-embedded descendant references");
        }
    }
    return true;
}

} // namespace

bool validate_blueprint_name_for_extract(const bp2::Blueprint& source,
                                         const std::string& blueprint_name,
                                         ui::StringInterner& interner,
                                         ui::InternedId* blueprint_iid_out,
                                         std::string* error_out) {
    if (blueprint_name.empty()) {
        return set_error(error_out, "extract blueprint name must be non-empty");
    }

    const ui::InternedId blueprint_iid = blueprint_iid_out
        ? interner.intern(blueprint_name)
        : interner.lookup(blueprint_name);
    for (const auto& n : source.nested()) {
        const bool matches = !blueprint_iid.empty()
            ? (n.blueprint_id() == blueprint_iid)
            : (interner.resolve(n.blueprint_id()) == blueprint_name);
        if (matches) {
            return set_error(error_out, "blueprint name already exists in nested definitions");
        }
    }
    for (const auto& n : source.nodes()) {
        if (n.view.name == blueprint_name) {
            return set_error(error_out, "blueprint name already exists as node name");
        }
    }

    if (blueprint_iid_out) {
        *blueprint_iid_out = blueprint_iid;
    }
    return true;
}

DescendantRemapStats collect_descendant_remap_stats(
    const bp2::Blueprint& source,
    const std::unordered_set<ui::InternedId>& selected_set,
    bool allow_nonembedded_descendant_refs) {
    DescendantRemapStats stats;

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

    std::function<void(const bp2::Blueprint::Nested&)> visit_nested;
    visit_nested = [&](const bp2::Blueprint::Nested& owner) {
        if (!owner.inline_def()) {
            return;
        }
        for (const auto& child : owner.inline_def()->nested()) {
            if (!child.is_embedded()) {
                if (embedded_by_blueprint_id.find(child.blueprint_id()) != embedded_by_blueprint_id.end()) {
                    ++stats.remapped;
                } else if (allow_nonembedded_descendant_refs) {
                    ++stats.passthrough;
                }
            }
            visit_nested(child);
        }
    };

    for (const auto& n : source.nested()) {
        if (selected_set.find(n.id) == selected_set.end()) {
            continue;
        }
        if (!n.is_embedded() || !n.inline_def()) {
            continue;
        }
        visit_nested(n);
    }

    return stats;
}

std::optional<ExtractionPlan> analyze_selection(const bp2::Blueprint& bp,
                                                 const std::vector<ui::InternedId>& selected_ids,
                                                 const WindowScopeId& scope_id,
                                                 bool allow_nonembedded_descendant_refs,
                                                 ui::StringInterner& interner,
                                                 const bp2::PathArena& arena,
                                                 std::string* error_out) {
    ExtractionPlan plan;
    plan.selected_set.insert(selected_ids.begin(), selected_ids.end());
    if (plan.selected_set.size() < 2) {
        set_error(error_out, "extract requires at least 2 selected nodes");
        return std::nullopt;
    }

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = -std::numeric_limits<float>::max();
    float max_y = -std::numeric_limits<float>::max();

    for (const auto& node : bp.nodes()) {
        if (plan.selected_set.find(node.semantic.id) == plan.selected_set.end()) {
            continue;
        }
        if (node.semantic.owner_scope != scope_id.sim_scope_prefix()) {
            set_error(error_out, "selected nodes must belong to active group");
            return std::nullopt;
        }
        if (node.semantic.type == interner.intern("BlueprintInput")
            || node.semantic.type == interner.intern("BlueprintOutput")) {
            set_error(error_out, "extract does not support selecting BlueprintInput/BlueprintOutput bridge nodes");
            return std::nullopt;
        }
        const bp2::Blueprint::Nested* nested = bp.find_hosted_nested(node);
        if (nested) {
            if (!nested->is_embedded()) {
                set_error(error_out, "extract does not support selecting non-embedded nested instances");
                return std::nullopt;
            }
        } else if (node.view.expandable) {
            set_error(error_out, "extract nested instance metadata missing");
            return std::nullopt;
        }
        plan.internal_nodes.push_back(node);
        min_x = std::min(min_x, node.layout.x);
        min_y = std::min(min_y, node.layout.y);
        max_x = std::max(max_x, node.layout.x + node.layout.width.value_or(kDefaultNodeWidth));
        max_y = std::max(max_y, node.layout.y + node.layout.height.value_or(kDefaultNodeHeight));
    }

    if (plan.internal_nodes.size() < 2) {
        set_error(error_out, "selected nodes not found in blueprint");
        return std::nullopt;
    }

    plan.min_x = min_x;
    plan.min_y = min_y;
    plan.max_x = max_x;
    plan.max_y = max_y;
    plan.center_x = (min_x + max_x) * 0.5f;
    plan.center_y = (min_y + max_y) * 0.5f;

    for (const auto& wire : bp.wires()) {
        ui::InternedId src_node;
        ui::InternedId src_port;
        ui::InternedId tgt_node;
        ui::InternedId tgt_port;
        if (!path_to_node_port(wire.source, arena, src_node, src_port)
            || !path_to_node_port(wire.target, arena, tgt_node, tgt_port)) {
            set_error(error_out, "wire endpoint path unresolved during extraction");
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

        const bp2::Blueprint::Node* src_node_ptr = bp.find_node(src_node);
        const bp2::Blueprint::Node* tgt_node_ptr = bp.find_node(tgt_node);

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
            ec.port_type = find_port_type(bp, tgt_node_ptr, tgt_port);
            plan.inputs.push_back(std::move(ec));
        } else {
            ec.is_input = false;
            ec.external_node_id = tgt_node;
            ec.external_port = tgt_port;
            ec.internal_node_id = src_node;
            ec.internal_port = src_port;
            ec.iface_name = std::string(interner.resolve(src_port));
            ec.port_type = find_port_type(bp, src_node_ptr, src_port);
            plan.outputs.push_back(std::move(ec));
        }
    }

    std::sort(plan.inputs.begin(), plan.inputs.end(), compare_external);
    std::sort(plan.outputs.begin(), plan.outputs.end(), compare_external);

    dedupe_iface_names(plan.inputs, "in");
    dedupe_iface_names(plan.outputs, "out");

    if (!validate_selected_embedded_nested_merge_safety(
            bp,
            plan.selected_set,
            allow_nonembedded_descendant_refs,
            error_out)) {
        return std::nullopt;
    }

    return plan;
}

} // namespace editor::commands::extract_detail
