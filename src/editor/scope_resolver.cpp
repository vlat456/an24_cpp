#include "scope_resolver.h"
#include "blueprint_v2/blueprint/embedded_mutation.h"

namespace editor {

ResolvedScope resolve_scope(
    const WindowScopeId& scope_id,
    const bp2::EditorModel& model,
    const WindowManager& window_manager,
    const core::StringInterner& interner) {

    if (scope_id.is_external()) {
        // For external scopes, use the first path element as parent instance.
        // Multi-segment paths need externally pre-interned keys (not supported here).
        const core::InternedId scope_iid =
            scope_id.path().empty() ? core::InternedId{} : scope_id.path()[0];
        if (const BlueprintWindow* win = window_manager.find(scope_id)) {
            if (win->external_blueprint && win->external_interner) {
                return {
                    &*win->external_blueprint,
                    win->external_interner.get(),
                    external_ref_signal_context(scope_iid)
                };
            }
        }
        return {nullptr, nullptr, external_ref_signal_context(scope_iid)};
    }

    if (scope_id.is_embedded()) {
        const core::InternedId scope_iid =
            scope_id.path().empty() ? core::InternedId{} : scope_id.path()[0];
        // Use bp2:: directly — the editor:: overload in embedded_path_utils.h
        // is a thin wrapper that introduces ambiguity inside namespace editor.
        if (const bp2::Blueprint* embedded_bp = bp2::resolve_embedded_blueprint(
                model.current(), scope_id.path())) {
            return {
                embedded_bp,
                &interner,
                embedded_signal_context(scope_iid)
            };
        }
        return {nullptr, nullptr, embedded_signal_context(scope_iid)};
    }

    // Root scope
    return {&model.current(), &interner, root_signal_context()};
}

const bp2::Blueprint::Node* find_node_in_scope(
    const WindowScopeId& scope_id,
    core::InternedId node_id,
    const bp2::EditorModel& model,
    const WindowManager& window_manager,
    const core::StringInterner& interner) {

    if (node_id.empty()) return nullptr;

    const ResolvedScope resolved = resolve_scope(scope_id, model, window_manager, interner);
    if (!resolved.blueprint || !resolved.interner) {
        return nullptr;
    }
    return resolved.blueprint->find_node(node_id);
}

} // namespace editor
