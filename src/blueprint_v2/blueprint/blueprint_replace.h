#pragma once

/// Order-preserving blueprint replacement helpers and tri-state mutation result.
/// Pure data operations on Blueprint — no EditorModel dependency.

#include "blueprint_v2/blueprint/blueprint.h"
#include "core/strings/interned_id.h"
#include <functional>

namespace bp2 {

/// Tri-state result for mutation operations.
enum class MutationResult {
    NotFound,
    NoChange,
    Changed,
};

/// Rebuild a blueprint replacing (or appending) a single node
/// while preserving the insertion order of all other elements.
Blueprint replace_node_preserve_order(const Blueprint& bp, Blueprint::Node updated);

/// Rebuild a blueprint replacing (or appending) a single wire
/// while preserving the insertion order of all other elements.
Blueprint replace_wire_preserve_order(const Blueprint& bp, Blueprint::Wire updated);

/// Find a node by id, apply mutation function, replace in blueprint.
/// Returns NotFound if node doesn't exist, NoChange if mutation was a no-op,
/// Changed if the blueprint was modified (bp is updated in-place).
MutationResult try_update_node(Blueprint& bp,
                               core::InternedId id,
                               const std::function<void(Blueprint::Node&)>& fn);

/// Find a wire by id, apply mutation function, replace in blueprint.
/// Returns NotFound if wire doesn't exist, NoChange if mutation was a no-op,
/// Changed if the blueprint was modified (bp is updated in-place).
MutationResult try_update_wire(Blueprint& bp,
                               core::InternedId id,
                               const std::function<void(Blueprint::Wire&)>& fn);

} // namespace bp2