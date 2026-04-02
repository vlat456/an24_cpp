#pragma once

#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstring>
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
// JIT Provider - runtime port index lookup from flat array
// =============================================================================

struct JitProvider {
    /// Sentinel value for unmapped ports — guaranteed out-of-bounds
    static constexpr uint32_t UNMAPPED = UINT32_MAX;

    /// Flat array indexed by PortNames enum value. Size = PortNames::_COUNT.
    /// Each slot holds the signal index for that port, or UNMAPPED if unused.
    /// Cache-friendly O(1) lookup replacing std::unordered_map.
    uint32_t indices[static_cast<size_t>(PortNames::_COUNT)];

    /// Default-initialize all slots to UNMAPPED.
    JitProvider() {
        std::memset(indices, 0xFF, sizeof(indices));  // UINT32_MAX = 0xFFFFFFFF
    }

    /// Runtime lookup from flat array populated during JSON parsing.
    /// Returns UNMAPPED (UINT32_MAX) if port is not mapped.
    /// Debug builds: assert fires immediately.
    /// Release builds: logs once to stderr so the issue is diagnosable.
    uint32_t get(PortNames p) const {
        uint32_t idx = static_cast<uint32_t>(p);
        assert(idx < static_cast<uint32_t>(PortNames::_COUNT) && "PortNames out of range");
        uint32_t val = indices[idx];
        if (val != UNMAPPED) {
            return val;
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
        uint32_t idx = static_cast<uint32_t>(p);
        return idx < static_cast<uint32_t>(PortNames::_COUNT) && indices[idx] != UNMAPPED;
    }

    /// Add port mapping during JSON parsing
    void set(PortNames p, uint32_t signal_idx) {
        uint32_t idx = static_cast<uint32_t>(p);
        assert(idx < static_cast<uint32_t>(PortNames::_COUNT) && "PortNames out of range");
        indices[idx] = signal_idx;
    }
};
