#pragma once

/// Scope resolver — resolves a WindowScopeId to a Blueprint + interner + context.
///
/// Pure scope-resolution + model query. No simulation state involved.
/// Used by SimulationBridge (signal key resolution), Document (node lookups,
/// sub-window opening), and WindowSystem (node queries).
///
/// These are free functions rather than member functions to make the
/// dependency graph explicit: they need (EditorModel, WindowManager,
/// StringInterner) and nothing else.

#include "signal_key_resolver.h"
#include "window/window_manager.h"
#include "window/window_scope_id.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "core/strings/interned_id.h"

namespace editor {

/// Result of resolving a WindowScopeId against the editor model.
/// Carries the blueprint, its interner, and the signal key context.
struct ResolvedScope {
    const bp2::Blueprint* blueprint = nullptr;
    const core::StringInterner* interner = nullptr;
    SignalKeyContext context = root_signal_context();
};

/// Resolve a WindowScopeId to a (Blueprint*, StringInterner*, SignalKeyContext).
///
/// - Root scope → root blueprint + document interner
/// - Embedded scope → embedded blueprint + document interner
/// - External scope → external blueprint from WindowManager + external interner
[[nodiscard]] ResolvedScope resolve_scope(
    const WindowScopeId& scope_id,
    const bp2::EditorModel& model,
    const WindowManager& window_manager,
    const core::StringInterner& interner);

/// Find a node by id within a scoped blueprint.
/// Returns nullptr if the scope or node does not exist.
[[nodiscard]] const bp2::Blueprint::Node* find_node_in_scope(
    const WindowScopeId& scope_id,
    core::InternedId node_id,
    const bp2::EditorModel& model,
    const WindowManager& window_manager,
    const core::StringInterner& interner);

} // namespace editor
