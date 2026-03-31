#pragma once

#include <unordered_map>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <vector>

// Full definition of PortNames (lightweight, no component dependencies)
#include "port_names.h"

// =============================================================================
// Provider Pattern for Zero-Overhead AOT vs Flexible JIT
// =============================================================================

/// AOT Provider - compile-time constexpr port index lookup
/// Generates direct array access: state.values[0] instead of state.values[this->v_in_idx]
template <PortNames P, uint32_t Idx>
struct Binding {
    static constexpr PortNames key = P;
    static constexpr uint32_t value = Idx;
};

template <typename... Bindings>
struct AotProvider {
    /// Compile-time constexpr lookup - compiler optimizes to constant!
    /// Returns UINT32_MAX if port is not in the binding list (same sentinel as JitProvider).
    static constexpr uint32_t get(PortNames p) {
        uint32_t result = UINT32_MAX;
        // Fold expression: try each binding until match found
        // Compiler fully optimizes to single constant at compile-time
        ((p == Bindings::key ? (result = Bindings::value, void()) : void()), ...);
        return result;
    }

    /// Check if a port is in the binding list (compile-time constant).
    /// Returns true if port maps to a valid index (not UINT32_MAX sentinel).
    static constexpr bool has(PortNames p) {
        return get(p) != UINT32_MAX;
    }
};

// =============================================================================
// JIT Provider - runtime port index lookup from JSON
// =============================================================================

struct JitProvider {
    /// Sentinel value for unmapped ports — guaranteed out-of-bounds
    static constexpr uint32_t UNMAPPED = UINT32_MAX;

    std::unordered_map<PortNames, uint32_t> indices;

    /// Runtime lookup from map populated during JSON parsing.
    /// Returns UNMAPPED (UINT32_MAX) if port is not mapped.
    /// Debug builds: assert fires immediately.
    /// Release builds: logs once to stderr so the issue is diagnosable.
    uint32_t get(PortNames p) const {
        auto it = indices.find(p);
        if (it != indices.end()) {
            return it->second;
        }
        assert(false && "JitProvider::get() called with unmapped port");
        // Release-mode fallback: log to stderr so the problem is visible
        // even when asserts are compiled out. This path is cold.
        [[maybe_unused]] static bool warned = [] {
            std::fprintf(stderr, "[JitProvider] ERROR: get() called with unmapped port — returning sentinel\n");
            return true;
        }();
        return UNMAPPED;
    }

    /// Check if a port is mapped
    bool has(PortNames p) const {
        return indices.find(p) != indices.end();
    }

    /// Add port mapping during JSON parsing
    void set(PortNames p, uint32_t idx) {
        indices[p] = idx;
    }
};
