#pragma once

#include "component.h"
#include "../json_parser/json_parser.h"
#include <vector>
#include <cstdint>
#include <cmath>

namespace an24 {

/// Signal metadata
struct SignalType {
    Domain domain;
    bool is_fixed;
};

/// Simulation state - all physics arrays (Structure of Arrays)
/// Signals are arranged: [dynamic signals first] [fixed signals at end]
/// This enables branchless iteration - no is_fixed checks needed!
struct SimulationState {
    /// Across variables: potentials (V), pressures, temperatures
    alignas(64) std::vector<float> across;

    /// Through variables: flows (I), flow rates, heat flux
    alignas(64) std::vector<float> through;

    /// Conductance - accumulates each iteration
    alignas(64) std::vector<float> conductance;

    /// Inverse conductance (precomputed 1/G)
    alignas(64) std::vector<float> inv_conductance;

    /// Pre-allocated convergence buffer for zero-allocation comparison
    alignas(64) std::vector<float> convergence_buffer;

    /// Signal metadata (for diagnostics, not used in hot path!)
    std::vector<SignalType> signal_types;

    /// Dynamic signals count - signals [0..count) are dynamic
    /// Signals [count..size) are fixed - iterate only up to count!
    uint32_t dynamic_signals_count = 0;

    /// Default constructor
    SimulationState() = default;

    /// Allocate a new signal
    /// IMPORTANT: Fixed signals are placed at END of array!
    [[nodiscard]] uint32_t allocate_signal(float initial_value, SignalType type);

    /// Resize all buffers to match signal count
    void resize_buffers(uint32_t signal_count);

    /// Clear through[] and conductance[] accumulators
    void clear_through();

    /// Precompute inverse conductance (1/G)
    void precompute_inv_conductance();

    /// SOR solver with delta clamping and NaN/Inf protection
    void solve_signals_balance(float sor_omega);

    /// Save current state for convergence checking
    void save_convergence_state();

    /// Check convergence - compare current vs saved
    bool has_converged(float tolerance) const;

    /// Get max change since last save
    float get_max_change() const;
};

// ==============================================================================
// Norton stamping helpers - optimized inline functions for common patterns
// These reduce code duplication and enable better compiler optimization
// ==============================================================================

/// Stamp a two-port conductance between two nodes (most common pattern)
/// Residual convention: through[i] > 0 means voltage at i should increase
/// Branch current INTO idx1 = (V2 - V1) * g  (positive when V2 > V1)
inline void stamp_two_port(
    float* __restrict conductance,
    float* __restrict through,
    const float* __restrict across,
    uint32_t idx1,
    uint32_t idx2,
    float g
) {
    float i = (across[idx2] - across[idx1]) * g;
    conductance[idx1] += g;
    conductance[idx2] += g;
    through[idx1] += i;
    through[idx2] -= i;
}

/// Stamp a one-port to ground (for loads, sensors)
/// Current flows from node to ground: residual = -V*g (drains voltage)
inline void stamp_one_port_ground(
    float* __restrict conductance,
    float* __restrict through,
    const float* __restrict across,
    uint32_t idx,
    float g
) {
    float i = across[idx] * g;
    conductance[idx] += g;
    through[idx] -= i;
}

/// Stamp a current source (Norton: current source + parallel conductance)
/// Adds g to conductance, adds I_source to through
inline void stamp_current_source(
    float* __restrict conductance,
    float* __restrict through,
    uint32_t idx,
    float g,
    float i_source
) {
    conductance[idx] += g;
    through[idx] += i_source;
}

/// Stamp voltage source (Thevenin equivalent: voltage source in series with R)
/// Converts to Norton: I_source = V / R, G = 1/R
inline void stamp_voltage_source(
    float* __restrict conductance,
    float* __restrict through,
    const float* __restrict across,
    uint32_t idx,
    float v_source,
    float r_internal
) {
    float g = 1.0f / r_internal;
    float i = (v_source - across[idx]) * g;
    conductance[idx] += g;
    through[idx] += i;
}

} // namespace an24
