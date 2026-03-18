#pragma once

#include <unordered_map>
#include <cstdint>
#include <cassert>
#include <vector>

// Forward declarations (PortNames defined in port_registry.h)
enum class PortNames : uint32_t;

// =============================================================================
// Provider Pattern for Zero-Overhead AOT vs Flexible JIT
// =============================================================================

/// AOT Provider - compile-time constexpr port index lookup
/// Generates direct array access: state.across[0] instead of state.across[this->v_in_idx]
template <PortNames P, uint32_t Idx>
struct Binding {
    static constexpr PortNames key = P;
    static constexpr uint32_t value = Idx;
};

template <typename... Bindings>
struct AotProvider {
    /// Compile-time constexpr lookup - compiler optimizes to constant!
    static constexpr uint32_t get(PortNames p) {
        uint32_t result = 0;
        // Fold expression: try each binding until match found
        // Compiler fully optimizes to single constant at compile-time
        ((p == Bindings::key ? (result = Bindings::value, void()) : void()), ...);
        return result;
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
    /// Returns UNMAPPED (UINT32_MAX) if port is not mapped — debug assert catches misuse.
    uint32_t get(PortNames p) const {
        auto it = indices.find(p);
        if (it != indices.end()) {
            return it->second;
        }
        assert(false && "JitProvider::get() called with unmapped port");
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
