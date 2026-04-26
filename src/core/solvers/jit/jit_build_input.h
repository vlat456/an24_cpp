#pragma once

/// @file jit_build_input.h
/// Lightweight data-only header for JitBuildInput.
/// Extracted from jit_solver.h to avoid pulling all.h through port_registry.h
/// in consumers that only need the build-input struct (e.g. sim_export.h).

#include "core/model/resolved_device.h"
#include "core/model/component_types.h"
#include "core/strings/interned_id.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/// Typed port-to-signal mapping. Keys are interned "node_id.port_name" strings.
/// Runtime lookups are integer-only (InternedId comparison, no string hashing).
using PortToSignal = std::unordered_map<core::InternedId, uint32_t>;

/// Pre-computed build input for the JIT solver.
/// Can be produced either from:
///   - elaborate_for_jit(FlatNetlist, ...)  — canonical BP2 path (no JSON)
///   - build_input_from_json(string)        — legacy JSON adapter (tests, CLI)
struct JitBuildInput {
    std::vector<ResolvedDevice> devices;
    std::vector<BridgePortDefinition> bridge_ports;
    PortToSignal port_to_signal;
    core::StringInterner signal_key_interner;
    uint32_t signal_count = 0;
    std::unordered_map<std::string, float> initial_values;
};
