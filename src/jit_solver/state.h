#pragma once

#include "../json_parser/json_parser.h"
#include <cstdint>
#include <vector>

/// Signal metadata.
struct SignalType {
    Domain domain;
    bool is_fixed;
};

/// Simulation state for push propagation.
/// Single values[] array stores all signal values.
struct SimulationState {
    alignas(64) std::vector<float> values;
    std::vector<SignalType> signal_types;

    alignas(64) std::vector<float> lut_keys;
    alignas(64) std::vector<float> lut_values;

    // Number of non-fixed signals allocated so far.
    // Allocation is append-only so returned indices remain stable.
    uint32_t dynamic_signals_count = 0;

    SimulationState() = default;

    [[nodiscard]] uint32_t allocate_signal(float initial_value, SignalType type);
};
