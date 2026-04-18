#include "extract_blueprint.h"

#include "extract_blueprint_internal.h"
#include "editor/visual/persist.h"

namespace editor::commands {

std::optional<bp2::Blueprint> build_extracted_blueprint_atomic(
    const bp2::Blueprint& source,
    const std::vector<ui::InternedId>& selected_node_ids,
    const std::string& blueprint_name,
    const WindowScopeId& scope_id,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    const TypeRegistry& parser_registry,
    std::string* error_out,
    bool allow_nonembedded_descendant_refs) {
    (void)parser_registry;

    ui::InternedId blueprint_iid;
    if (!extract_detail::validate_blueprint_name_for_extract(
            source, blueprint_name, interner, &blueprint_iid, error_out)) {
        return std::nullopt;
    }

    auto plan = extract_detail::analyze_selection(
        source,
        selected_node_ids,
        scope_id,
        allow_nonembedded_descendant_refs,
        interner,
        arena,
        parser_registry,
        error_out);
    if (!plan) {
        return std::nullopt;
    }

    auto updated = extract_detail::build_parent_blueprint_from_plan(
        source,
        *plan,
        blueprint_iid,
        blueprint_name,
        scope_id,
        allow_nonembedded_descendant_refs,
        interner,
        arena,
        error_out);
    if (!updated) {
        return std::nullopt;
    }

    return *updated;
}

std::optional<ExtractToBlueprintPreview> build_extract_to_blueprint_preview(
    const bp2::Blueprint& source,
    const std::vector<ui::InternedId>& selected_node_ids,
    const std::string& blueprint_name,
    const WindowScopeId& scope_id,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    const TypeRegistry& parser_registry,
    std::string* error_out,
    bool allow_nonembedded_descendant_refs) {
    if (!extract_detail::validate_blueprint_name_for_extract(
            source, blueprint_name, interner, nullptr, error_out)) {
        return std::nullopt;
    }

    auto plan = extract_detail::analyze_selection(
        source,
        selected_node_ids,
        scope_id,
        allow_nonembedded_descendant_refs,
        interner,
        arena,
        parser_registry,
        error_out);
    if (!plan) {
        return std::nullopt;
    }

    ExtractToBlueprintPreview preview;
    preview.selected_nodes = plan->internal_nodes.size();
    preview.internal_wires = plan->internal_wires.size();
    preview.input_count = plan->inputs.size();
    preview.output_count = plan->outputs.size();
    for (const auto& ec : plan->inputs) {
        preview.input_iface_names.push_back(ec.iface_name);
    }
    for (const auto& ec : plan->outputs) {
        preview.output_iface_names.push_back(ec.iface_name);
    }

    std::unordered_set<std::string> inputs(preview.input_iface_names.begin(), preview.input_iface_names.end());
    for (const auto& name : preview.output_iface_names) {
        if (inputs.find(name) != inputs.end()) {
            preview.iface_collision_names.push_back(name);
        }
    }
    std::sort(preview.iface_collision_names.begin(), preview.iface_collision_names.end());
    preview.iface_collision_names.erase(
        std::unique(preview.iface_collision_names.begin(), preview.iface_collision_names.end()),
        preview.iface_collision_names.end());

    const auto remap_stats = extract_detail::collect_descendant_remap_stats(
        source,
        plan->selected_set,
        allow_nonembedded_descendant_refs);
    preview.remapped_descendant_refs = remap_stats.remapped;
    preview.passthrough_descendant_refs = remap_stats.passthrough;
    return preview;
}

} // namespace editor::commands
