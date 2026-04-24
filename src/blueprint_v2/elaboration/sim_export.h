#pragma once

#include "blueprint_v2/flattener/flat_netlist.h"
#include "blueprint_v2/path/path.h"
#include "core/model/component_registry.h"
#include "core/solvers/jit/jit_solver.h"
#include "ui/core/interned_id.h"

#include <vector>

namespace bp2::elaboration {

/// Convert a FlatNetlist component path (hierarchical Path) to a colon-separated
/// node_id string. Used by elaborate_for_jit and test infrastructure.
std::string node_id_from_path(Path node_path, PathArena& arena, const ui::StringInterner& interner);

/// Compute the parent-facing signal key for a bridge component.
/// For a bridge node "parent:bridge_id" with exposed_port_name "port",
/// returns "parent.port". Returns "" if the node_id has no colon separator.
std::string exposed_key_for_bridge(
    std::string_view bridge_dev_id,
    const ui::InternedId& exposed_port_name,
    const ui::StringInterner& interner);

/// Extract BridgePortDefinitions from a FlatNetlist.
/// Bridge components (non-empty `exposed_port_name`) are converted to their
/// equivalent BridgePortDefinition with direction and type inferred from port descriptors.
std::vector<BridgePortDefinition> extract_bridge_definitions(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner);

/// Convert a FlatNetlist directly to JitBuildInput (no JSON intermediate).
/// This is the canonical path for BP2 → JIT runtime.
JitBuildInput elaborate_for_jit(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner,
    const ComponentRegistry& type_registry);

} // namespace bp2::elaboration
