#pragma once

/// Embedded-path resolution and mutation utilities.
/// Provides canonical InternedId implementations and string-based convenience
/// overloads for walking, resolving, and mutating deeply-nested embedded
/// blueprints within a root Blueprint.

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace editor {

enum class EmbeddedMutationResultKind {
    PathNotFound,
    NoChange,
    Changed,
};

struct EmbeddedMutationResult {
    EmbeddedMutationResultKind kind = EmbeddedMutationResultKind::PathNotFound;
    std::optional<bp2::Blueprint> blueprint;
};

/// Result of resolving an embedded path to its terminal node.
struct ResolvedEmbeddedNode {
    const bp2::Blueprint* parent_blueprint = nullptr;
    const bp2::Blueprint::Node* node = nullptr;
};

// ============================================================================
// Typed InternedId path variants — canonical implementations.
// String-based overloads below delegate here.
// ============================================================================

/// Resolve an embedded blueprint by walking a typed InternedId path.
const bp2::Blueprint* resolve_embedded_blueprint_by_id(
    const bp2::Blueprint& root_bp,
    std::span<const ui::InternedId> path);

/// Resolve the host node at the end of a typed InternedId path.
ResolvedEmbeddedNode resolve_embedded_node_by_id(
    const bp2::Blueprint& root_bp,
    std::span<const ui::InternedId> path);

/// Check whether a full typed InternedId path still exists in the root blueprint.
bool embedded_path_exists_by_id(
    const bp2::Blueprint& root_bp,
    std::span<const ui::InternedId> path);

/// Apply a mutation to the embedded blueprint at the end of an InternedId path,
/// then propagate the change back up through all ancestor nodes to produce a
/// new root Blueprint. Returns std::nullopt if the path cannot be resolved.
EmbeddedMutationResult mutate_embedded_blueprint_by_id(
    bp2::Blueprint root_bp,
    std::span<const ui::InternedId> path,
    const std::function<bp2::Blueprint(const bp2::Blueprint&)>& mutation);

// ============================================================================
// String-based convenience overloads — intern and delegate to canonical
// InternedId versions above.
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
