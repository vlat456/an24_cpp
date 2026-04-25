#pragma once

/// @file elaboration_utils.h
/// Lightweight shared utilities used by both JIT and codegen elaboration paths.
/// No dependency on jit_solver.h — safe to include from codegen without
/// pulling in the entire JIT runtime.

#include "blueprint_v2/flattener/flat_netlist.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"

#include <string>
#include <string_view>

namespace ui { class StringInterner; }

namespace bp2::elaboration {

/// Convert a FlatNetlist component path (hierarchical Path) to a colon-separated
/// node_id string. Used by elaborate_for_jit, elaborate_for_codegen, and test infrastructure.
std::string node_id_from_path(Path node_path, PathArena& arena, const ui::StringInterner& interner);

/// Compute the parent-facing signal key for a bridge component.
/// For a bridge node "parent:bridge_id" with exposed_port_name "port",
/// returns "parent.port". Returns "" if the node_id has no colon separator.
std::string exposed_key_for_bridge(
    std::string_view bridge_dev_id,
    const ui::InternedId& exposed_port_name,
    const ui::StringInterner& interner);

} // namespace bp2::elaboration
