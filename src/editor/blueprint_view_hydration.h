#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "ui/core/interned_id.h"
#include "json_parser/json_parser.h"
#include "data/node_content.h"

namespace editor {

/// [DRY-hydration] Hydrate runtime-only view state (ViewData tier 2) for a
/// single node in-place.  This is the **single authoritative location** for
/// render_hint and content_* population; all call sites (creation, load,
/// import) must go through here.
///
/// Only component-kind nodes are hydrated; blueprint-instance nodes are
/// skipped because their view state is derived differently.
///
/// [Issue #132] Now derives param-driven content from instance params first,
/// then falls back to type definition defaults. This ensures that edited
/// params (e.g., knob positions, slider min/max) take effect immediately
/// after inspector edits or load/import.
///
/// @param node     The node to hydrate (mutated in-place).
/// @param def      TypeDefinition for the node's component type (may be null).
/// @param interner StringInterner for resolving param keys.
inline void hydrate_node_view(bp2::Blueprint::Node& node,
                              const TypeDefinition* def,
                              ui::StringInterner& interner) {
    if (!def) return;
    node.view.render_hint = def->render_hint;
    NodeContent nc = create_node_content(def, node.semantic.params, node.semantic.string_params, interner);
    node.view.content_type    = nc.type;
    node.view.content_label   = nc.label;
    node.view.content_value   = nc.value;
    node.view.content_min     = nc.min;
    node.view.content_max     = nc.max;
    node.view.content_unit    = nc.unit;
    node.view.content_state   = nc.state;
    node.view.content_tripped = nc.tripped;
}

/// [DRY-hydration] Hydrate runtime-only node view state from TypeRegistry.
/// Fills render_hint and content_* fields (not persisted in canonical v1 format).
/// Recursively hydrates embedded inline blueprints.
/// @param bp Blueprint to hydrate
/// @param interner StringInterner for resolving node type names
/// @param registry TypeRegistry for looking up TypeDefinition
/// @return Hydrated blueprint
inline bp2::Blueprint hydrate_runtime_node_view_data(
    bp2::Blueprint bp,
    ui::StringInterner& interner,
    const TypeRegistry& registry) {
    const std::vector<bp2::Blueprint::Node> snapshot = bp.nodes();
    for (const auto& node : snapshot) {
        bp2::Blueprint::Node updated = node;

        if (node.is_component()) {
            const std::string type_name(interner.resolve(node.semantic.type));
            hydrate_node_view(updated, registry.get(type_name), interner);
        }

        // Recursively hydrate embedded inline blueprints
        if (updated.source && updated.source->is_embedded()) {
            if (const bp2::Blueprint* inline_def = updated.source->inline_def()) {
                updated.source->set_inline_def(std::make_unique<bp2::Blueprint>(
                    hydrate_runtime_node_view_data(*inline_def, interner, registry)));
            }
        }

        bp = bp2::replace_node_preserve_order(bp, std::move(updated));
    }
    return bp;
}

} // namespace editor
