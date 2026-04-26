#pragma once

/// Embedded-path operations — InternedId-native, zero string construction.
///
/// Delegates to the canonical bp2:: implementations in
/// blueprint_v2/blueprint/embedded_mutation.h. Callers pass InternedId
/// paths directly (from WindowScopeId::path() or manual construction).

#include "blueprint_v2/blueprint/embedded_mutation.h"
#include "core/strings/interned_id.h"

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace editor {

// Re-export bp2 types for editor callers.
using EmbeddedMutationResultKind = bp2::EmbeddedMutationResultKind;
using EmbeddedMutationResult = bp2::EmbeddedMutationResult;
using ResolvedEmbeddedNode = bp2::ResolvedEmbeddedNode;

/// Convert a string-based scope path to InternedId path via lookup.
/// Returns std::nullopt if any segment is not interned.
/// Only for use at serialization boundaries — all internal code should
/// construct InternedId paths directly.
std::optional<std::vector<core::InternedId>> intern_scope_path(
    const core::StringInterner& interner,
    std::span<const std::string> scope_path);

/// Resolve an embedded blueprint by walking an InternedId scope path.
inline const bp2::Blueprint* resolve_embedded_blueprint(
    const bp2::Blueprint& root_bp,
    std::span<const core::InternedId> scope_path) {
    return bp2::resolve_embedded_blueprint(root_bp, scope_path);
}

/// Resolve the host node at the end of an InternedId scope path.
inline ResolvedEmbeddedNode resolve_embedded_node(
    const bp2::Blueprint& root_bp,
    std::span<const core::InternedId> scope_path) {
    return bp2::resolve_embedded_node(root_bp, scope_path);
}

/// Check whether an InternedId scope path still exists in the root blueprint.
inline bool embedded_path_exists(
    const bp2::Blueprint& root_bp,
    std::span<const core::InternedId> scope_path) {
    return bp2::resolve_embedded_blueprint(root_bp, scope_path) != nullptr;
}

/// Apply a mutation to the embedded blueprint at the end of an InternedId scope path.
inline EmbeddedMutationResult mutate_embedded_blueprint(
    bp2::Blueprint root_bp,
    std::span<const core::InternedId> scope_path,
    const std::function<bp2::Blueprint(const bp2::Blueprint&)>& mutation) {
    return bp2::mutate_embedded_blueprint(std::move(root_bp), scope_path, mutation);
}

} // namespace editor
