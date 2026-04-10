#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "ui/core/interned_id.h"
#include "json_parser/json_parser.h"
#include "data/node_content.h"

namespace editor {

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
            const TypeDefinition* def = registry.get(type_name);
            if (def) {
                // Hydrate render_hint from type registry (not persisted in v1)
                updated.view.render_hint = def->render_hint;
                // Hydrate content from type definition
                NodeContent nc = create_node_content_from_def(def);
                updated.view.content_type    = nc.type;
                updated.view.content_label   = nc.label;
                updated.view.content_value   = nc.value;
                updated.view.content_min     = nc.min;
                updated.view.content_max     = nc.max;
                updated.view.content_unit    = nc.unit;
                updated.view.content_state   = nc.state;
                updated.view.content_tripped = nc.tripped;
            }
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
