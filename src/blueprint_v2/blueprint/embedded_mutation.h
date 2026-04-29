#pragma once

/// Embedded-path resolution and mutation for deeply-nested inline blueprints.
/// Pure data operations on bp2::Blueprint — no editor or StringInterner dependency.

#include "blueprint_v2/blueprint/blueprint.h"
#include "core/strings/interned_id.h"

#include <functional>
#include <optional>
#include <span>

namespace bp2 {

// ============================================================================
// Result types
// ============================================================================

/// Outcome of an embedded blueprint mutation.
enum class EmbeddedMutationResultKind {
    PathNotFound,
    NoChange,
    Changed,
};

/// Result of mutating an embedded blueprint: kind + optional new root blueprint.
struct EmbeddedMutationResult {
    EmbeddedMutationResultKind kind = EmbeddedMutationResultKind::PathNotFound;
    std::optional<Blueprint> blueprint;
};

/// Result of resolving an embedded path to its terminal node.
/// All pointers are into the caller's Blueprint — valid until it is mutated.
struct ResolvedEmbeddedNode {
    const Blueprint* parent_blueprint = nullptr;
    const Blueprint::Node* node = nullptr;
};

// ============================================================================
// Path walking (read-only)
// ============================================================================

/// Resolve an embedded blueprint by walking an InternedId path through
/// inline blueprint chains. Returns nullptr if any segment is not an
/// embedded blueprint-instance node.
const Blueprint* resolve_embedded_blueprint(
    const Blueprint& root_bp,
    std::span<const core::InternedId> path);

/// Resolve the host node at the end of an InternedId path.
/// Returns {parent_blueprint, target_node}. parent_blueprint is the blueprint
/// containing the target node (or nullptr if resolution fails).
/// Pointers are into root_bp — stable until root_bp is mutated.
ResolvedEmbeddedNode resolve_embedded_node(
    const Blueprint& root_bp,
    std::span<const core::InternedId> path);

/// Find only the terminal node at the end of an InternedId path.
/// Returns pointer into root_bp, or nullptr if resolution fails.
/// Lighter-weight than resolve_embedded_node() when the parent blueprint
/// is not needed.
const Blueprint::Node* find_embedded_node(
    const Blueprint& root_bp,
    std::span<const core::InternedId> path);

/// Check whether a full InternedId path still exists in the root blueprint.
bool embedded_path_exists(
    const Blueprint& root_bp,
    std::span<const core::InternedId> path);

// ============================================================================
// Mutation (produces new root Blueprint)
// ============================================================================

/// Apply a mutation to the embedded blueprint at the end of an InternedId path,
/// then propagate the change back up through all ancestor nodes to produce a
/// new root Blueprint. Returns PathNotFound if the path cannot be resolved,
/// NoChange if the mutation produced an identical blueprint, Changed with the
/// new root otherwise.
EmbeddedMutationResult mutate_embedded_blueprint(
    Blueprint root_bp,
    std::span<const core::InternedId> path,
    const std::function<Blueprint(const Blueprint&)>& mutation);

} // namespace bp2
