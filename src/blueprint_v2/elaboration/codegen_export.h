#pragma once

#include "blueprint_v2/flattener/flat_netlist.h"
#include "blueprint_v2/path/path.h"
#include "core/model/component_registry.h"
#include "core/model/resolved_device.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ui { class StringInterner; }

namespace bp2::elaboration {

/// Codegen-ready build input produced from a FlatNetlist.
///
/// Signal indices come directly from compact_signals() — no UnionFind needed.
/// Keys are plain strings ("node_id.port_name") matching codegen's existing convention.
struct CodegenBuildInput {
    std::vector<ResolvedDevice> devices;
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t signal_count = 0;
};

/// Convert a FlatNetlist to codegen-ready build input.
///
/// Mirrors elaborate_for_jit() but produces string-keyed port_to_signal
/// (codegen uses string lookups, not InternedId). Skips bridge nodes and
/// visual-only devices — only simulation-relevant primitives are emitted.
CodegenBuildInput elaborate_for_codegen(
    const FlatNetlist& netlist,
    PathArena& arena,
    core::StringInterner& interner,
    const ComponentRegistry& type_registry);

} // namespace bp2::elaboration
