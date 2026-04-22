#pragma once

/// String-based convenience overloads for embedded-path operations.
/// Interns string paths and delegates to the canonical bp2:: implementations
/// in blueprint_v2/blueprint/embedded_mutation.h.

#include "blueprint_v2/blueprint/embedded_mutation.h"
#include "ui/core/interned_id.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ui {
class StringInterner;
}

namespace editor {

// Re-export bp2 types for backward compatibility with editor callers.
using EmbeddedMutationResultKind = bp2::EmbeddedMutationResultKind;
using EmbeddedMutationResult = bp2::EmbeddedMutationResult;
using ResolvedEmbeddedNode = bp2::ResolvedEmbeddedNode;

// ============================================================================
// Path conversion helper
// ============================================================================

/// Convert a string-based scope path to InternedId path via lookup.
/// Returns std::nullopt if any segment is not interned.
std::optional<std::vector<ui::InternedId>> intern_scope_path(
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path);

// ============================================================================
// String-based convenience overloads — intern and delegate to bp2::.
// ============================================================================

/// Resolve an embedded blueprint by walking a string scope path.
const bp2::Blueprint* resolve_embedded_blueprint(
    const bp2::Blueprint& root_bp,
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path);

/// Resolve the host node at the end of a string scope path.
ResolvedEmbeddedNode resolve_embedded_node(
    const bp2::Blueprint& root_bp,
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path);

/// Check whether a full string scope path still exists in the root blueprint.
bool embedded_path_exists(
    const bp2::Blueprint& root_bp,
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path);

/// Apply a mutation to the embedded blueprint at the end of a string scope path.
EmbeddedMutationResult mutate_embedded_blueprint(
    bp2::Blueprint root_bp,
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path,
    const std::function<bp2::Blueprint(const bp2::Blueprint&)>& mutation);

} // namespace editor
