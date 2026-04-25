#pragma once

#include "blueprint_v2/elaboration/elaboration_utils.h"
#include "blueprint_v2/flattener/flat_netlist.h"
#include "blueprint_v2/path/path.h"
#include "core/model/component_registry.h"
#include "core/solvers/jit/jit_build_input.h"
#include "ui/core/interned_id.h"

#include <vector>

namespace bp2::elaboration {

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
