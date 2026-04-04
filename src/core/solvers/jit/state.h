#pragma once

#include "../../../json_parser/json_parser.h"
#include "subsolvers/subsolver_types.h"
#include <cstdint>
#include <vector>

/// Signal metadata.
struct SignalType {
    Domain domain;
    bool is_fixed;
};

/// Simulation state for push propagation.
/// Single values[] array stores all signal values.
///
/// NOTE (E-006): alignas(64) was previously applied to vector members here.
/// This is misleading: alignas on a std::vector aligns the *control block*
/// (3 pointers on the stack/struct), NOT the heap-allocated data buffer.
/// values.data() gets whatever alignment std::allocator provides (typically
/// 16 bytes on most platforms). If SIMD work is added in the future, use a
/// custom aligned allocator (e.g., std::vector<float, AlignedAllocator<64>>).
struct SimulationState {
    std::vector<float> values;
    std::vector<SignalType> signal_types;

    std::vector<float> lut_keys;
    std::vector<float> lut_values;

    // Number of non-fixed signals allocated so far.
    // Allocation is append-only so returned indices remain stable.
    uint32_t dynamic_signals_count = 0;

    // Pointer to currently active electrical runtime state.
    // Set by simulator before scheduler.step() each frame.
    // Null if electrical solving is not active.
    ElectricalRuntimeState* electrical_rt = nullptr;

    SimulationState() = default;

    [[nodiscard]] uint32_t allocate_signal(float initial_value, SignalType type);
};
