#pragma once

#include "component.h"
#include <vector>
#include <cstdint>

namespace an24 {

/// Signal metadata
struct SignalType {
    Domain domain;
    bool is_fixed;
};

/// Simulation state - all physics arrays (Structure of Arrays)
class SimulationState {
public:
    /// Across variables: potentials (V), pressures, temperatures
    std::vector<float> across;

    /// Through variables: flows (I), flow rates, heat flux
    std::vector<float> through;

    /// Conductance - accumulates each iteration
    std::vector<float> conductance;

    /// Inverse conductance (precomputed 1/G)
    std::vector<float> inv_conductance;

    /// Signal metadata
    std::vector<SignalType> signal_types;

    /// Default constructor
    SimulationState() = default;

    /// Allocate a new signal
    [[nodiscard]] uint32_t allocate_signal(float initial_value, SignalType type);

    /// Clear through[] and conductance[] accumulators
    void clear_through();

    /// Precompute inverse conductance (1/G)
    void precompute_inv_conductance();
};

} // namespace an24
