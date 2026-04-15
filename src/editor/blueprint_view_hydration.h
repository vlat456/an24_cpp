#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "ui/core/interned_id.h"
#include "json_parser/json_parser.h"
#include "data/node_content.h"

namespace editor {

/// [Issue #133] Hydrate **static** content semantics (ViewData tier 2) for a
/// single node in-place.  This is the **single authoritative location** for
/// render_hint and static content_* fields (type, label, min, max, unit).
///
/// Dynamic runtime state (content_value, content_state, content_tripped) is
/// intentionally NOT written here — those fields are owned exclusively by
/// the simulation runtime and user interaction.  Use
/// `initialize_node_content_defaults()` to set initial dynamic state at
/// node creation time.
///
/// [Issue #132] Derives param-driven static content from instance params
/// first, then falls back to type definition defaults.
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
    // Static semantics only — single authority for widget configuration.
    node.view.content_type    = nc.type;
    node.view.content_label   = nc.label;
    node.view.content_min     = nc.min;
    node.view.content_max     = nc.max;
    node.view.content_unit    = nc.unit;
    // content_value, content_state, content_tripped are NOT touched.
}

/// [Issue #133] Set initial dynamic state (value, state, tripped) from
/// semantic params.  Called ONLY at node creation / initial load — never
/// on re-hydration after inspector edits.
///
/// @param node     The node to initialize (mutated in-place).
/// @param def      TypeDefinition for the node's component type (may be null).
/// @param interner StringInterner for resolving param keys.
inline void initialize_node_content_defaults(bp2::Blueprint::Node& node,
                                             const TypeDefinition* def,
                                             ui::StringInterner& interner) {
    if (!def) return;
    NodeContent nc = create_node_content(def, node.semantic.params, node.semantic.string_params, interner);
    node.view.content_value   = nc.value;
    node.view.content_state   = nc.state;
    node.view.content_tripped = nc.tripped;
}

/// [Issue #133] Hydrate ALL content fields (static + dynamic) from a single
/// `create_node_content` call.  Use at node creation / initial load when no
/// runtime state exists yet.  Avoids the double-parse overhead of calling
/// `hydrate_node_view` + `initialize_node_content_defaults` separately.
///
/// @param node     The node to hydrate (mutated in-place).
/// @param def      TypeDefinition for the node's component type (may be null).
/// @param interner StringInterner for resolving param keys.
inline void hydrate_node_view_full(bp2::Blueprint::Node& node,
                                   const TypeDefinition* def,
                                   ui::StringInterner& interner) {
    if (!def) return;
    node.view.render_hint = def->render_hint;
    NodeContent nc = create_node_content(def, node.semantic.params, node.semantic.string_params, interner);
    // Static semantics
    node.view.content_type    = nc.type;
    node.view.content_label   = nc.label;
    node.view.content_min     = nc.min;
    node.view.content_max     = nc.max;
    node.view.content_unit    = nc.unit;
    // Dynamic defaults
    node.view.content_value   = nc.value;
    node.view.content_state   = nc.state;
    node.view.content_tripped = nc.tripped;
}

/// [Issue #133] Full initial hydration: static semantics + initial dynamic
/// defaults.  Used at load/import time when no runtime state exists yet.
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
            hydrate_node_view_full(updated, def, interner);
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
