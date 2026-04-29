#pragma once

#include "core/domain_types.h"
#include "core/solvers/common/nodal_types.h"
#include <cassert>
#include <cstdint>
#include <vector>

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

    std::vector<float> lut_keys;
    std::vector<float> lut_values;

    // Pointer to currently active electrical runtime state.
    // Set by simulator before scheduler.step() each frame.
    // Null if electrical solving is not active.
    NodalRuntimeState* electrical_rt = nullptr;

    // Pointer to currently active hydraulic runtime state.
    // Set by simulator before scheduler.step() each frame.
    // Null if hydraulic solving is not active.
    NodalRuntimeState* hydraulic_rt = nullptr;

    // Pointer to currently active pneumatic runtime state.
    // Set by simulator before scheduler.step() each frame.
    // Null if pneumatic solving is not active.
    NodalRuntimeState* pneumatic_rt = nullptr;

    SimulationState() = default;

    /// Allocate a new signal index and set its initial value.
    /// Returns the newly allocated index.
    [[nodiscard]] uint32_t allocate_signal(float initial_value) {
        const uint32_t idx = static_cast<uint32_t>(values.size());
        values.push_back(initial_value);
        return idx;
    }

    /// Bounds-checked signal read. Debug builds assert on out-of-range index.
    /// Release builds: zero overhead (inlined direct access).
    float signal(uint32_t idx) const {
        assert(idx < values.size() && "signal index out of bounds");
        return values[idx];
    }

    /// Bounds-checked signal write. Debug builds assert on out-of-range index.
    /// Release builds: zero overhead (inlined direct access).
    float& signal(uint32_t idx) {
        assert(idx < values.size() && "signal index out of bounds");
        return values[idx];
    }

};
