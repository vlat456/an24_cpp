#include "extract_blueprint.h"

#include "extract_blueprint_internal.h"
#include "blueprint_v2/editor_model/editor_model.h"
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
    ui::InternedId blueprint_iid;
    if (!extract_detail::validate_blueprint_name_for_extract(
            source, blueprint_name, interner, &blueprint_iid, error_out)) {
        return std::nullopt;
    }

    auto plan_opt = extract_detail::analyze_selection(
        source,
        selected_node_ids,
        scope_id,
        allow_nonembedded_descendant_refs,
        interner,
        arena,
        error_out);
    if (!plan_opt) {
        return std::nullopt;
    }
    const extract_detail::ExtractionPlan& plan = *plan_opt;

    auto out_opt = extract_detail::build_parent_blueprint_from_plan(
        source,
        plan,
        blueprint_iid,
        blueprint_name,
        scope_id,
        allow_nonembedded_descendant_refs,
        interner,
        arena,
        error_out);
    if (!out_opt) {
        return std::nullopt;
    }
    bp2::Blueprint out = bp2::canonicalize_composite_host_ifaces(std::move(*out_opt));

    std::string integrity_err;
    if (!validate_blueprint_integrity(out, interner, arena, parser_registry, &integrity_err)) {
        if (error_out) {
            *error_out = integrity_err;
        }
        return std::nullopt;
    }

    if (error_out) {
        error_out->clear();
    }
    return out;
}

std::optional<ExtractToBlueprintPreview> build_extract_to_blueprint_preview(
    const bp2::Blueprint& source,
    const std::vector<ui::InternedId>& selected_node_ids,
    const std::string& blueprint_name,
    const WindowScopeId& scope_id,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    std::string* error_out,
    bool allow_nonembedded_descendant_refs) {
    if (!extract_detail::validate_blueprint_name_for_extract(
            source, blueprint_name, interner, nullptr, error_out)) {
        return std::nullopt;
    }

    auto plan_opt = extract_detail::analyze_selection(
        source,
        selected_node_ids,
        scope_id,
        allow_nonembedded_descendant_refs,
        interner,
        arena,
        error_out);
    if (!plan_opt) {
        return std::nullopt;
    }

    const extract_detail::ExtractionPlan& plan = *plan_opt;
    ExtractToBlueprintPreview out;
    out.selected_nodes = plan.internal_nodes.size();
    out.internal_wires = plan.internal_wires.size();
    out.input_count = plan.inputs.size();
    out.output_count = plan.outputs.size();
    const extract_detail::DescendantRemapStats remap_stats =
        extract_detail::collect_descendant_remap_stats(
            source, plan.selected_set, allow_nonembedded_descendant_refs);
    out.remapped_descendant_refs = remap_stats.remapped;
    out.passthrough_descendant_refs = remap_stats.passthrough;
    out.input_iface_names.reserve(plan.inputs.size());
    out.output_iface_names.reserve(plan.outputs.size());

    std::unordered_set<std::string> in_names;
    in_names.reserve(plan.inputs.size());
    for (const auto& ec : plan.inputs) {
        out.input_iface_names.push_back(ec.iface_name);
        in_names.insert(ec.iface_name);
    }
    for (const auto& ec : plan.outputs) {
        out.output_iface_names.push_back(ec.iface_name);
        if (in_names.find(ec.iface_name) != in_names.end()) {
            out.iface_collision_names.push_back(ec.iface_name);
        }
    }
    std::sort(out.iface_collision_names.begin(), out.iface_collision_names.end());
    out.iface_collision_names.erase(
        std::unique(out.iface_collision_names.begin(), out.iface_collision_names.end()),
        out.iface_collision_names.end());

    if (error_out) {
        error_out->clear();
    }
    return out;
}

} // namespace editor::commands
