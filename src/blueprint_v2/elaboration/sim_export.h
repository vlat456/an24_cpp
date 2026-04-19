#pragma once

#include "blueprint_v2/flattener/flat_netlist.h"
#include "blueprint_v2/path/path.h"
#include "core/solvers/jit/jit_solver.h"
#include "ui/core/interned_id.h"

namespace bp2::elaboration {

/// Convert a FlatNetlist directly to JitBuildInput (no JSON intermediate).
/// This is the canonical path for BP2 → JIT runtime.
JitBuildInput elaborate_for_jit(
    const FlatNetlist& netlist,
    PathArena& arena,
    const ui::StringInterner& interner,
    const ComponentRegistry& type_registry);

} // namespace bp2::elaboration
