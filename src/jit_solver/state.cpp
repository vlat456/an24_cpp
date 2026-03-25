#include "state.h"
#include <algorithm>
#include <cmath>
#include <cstring>

uint32_t SimulationState::allocate_signal(float initial_value, SignalType type) {
    uint32_t idx = static_cast<uint32_t>(across.size());

    across.push_back(initial_value);
    through.push_back(0.0f);
    conductance.push_back(0.0f);
    inv_conductance.push_back(0.0f);
    signal_types.push_back(type);

    // Only dynamic signals count toward the iteration limit
    if (!type.is_fixed) {
        dynamic_signals_count = static_cast<uint32_t>(across.size());
    }

    return idx;
}

void SimulationState::resize_buffers(uint32_t signal_count) {
    convergence_buffer.resize(signal_count, 0.0f);
}

void SimulationState::clear_through() {
    // Use memset - faster than std::fill for small arrays
    std::memset(through.data(), 0, through.size() * sizeof(float));
    std::memset(conductance.data(), 0, conductance.size() * sizeof(float));
}

void SimulationState::precompute_inv_conductance() {
    // ================================================================
    // Branchless SOR safety: parasitic conductance + layout trick
    // ================================================================
    // Dynamic signals occupy indices [0 .. dynamic_signals_count).
    // Fixed signals occupy [dynamic_signals_count .. size).
    // SOR only iterates dynamic range, so fixed inv_conductance is never read.
    //
    // For dynamic nodes we add parasitic leakage (10 MOhm) to guarantee
    // total_g > 0 — no division-by-zero possible, no branch needed.
    // Floating nodes will relax toward 0 V (ground), which is physical.
    // ================================================================

    constexpr float PARASITIC_G = 1e-7f;

    // Dynamic signals: branchless, always safe (PARASITIC_G guarantees > 0)
    for (size_t i = 0; i < dynamic_signals_count; ++i) {
        float total_g = conductance[i] + PARASITIC_G;
        inv_conductance[i] = 1.0f / total_g;
    }

    // Fixed signals: zero out (SOR skips these, but keep array clean)
    for (size_t i = dynamic_signals_count; i < conductance.size(); ++i) {
        inv_conductance[i] = 0.0f;
    }
}

void SimulationState::save_convergence_state() {
    // Only copy dynamic_signals_count elements — that's the range get_max_change()
    // and has_converged() iterate. This also prevents overrunning convergence_buffer
    // if across.size() grew after resize_buffers() was called.
    size_t count = std::min(static_cast<size_t>(dynamic_signals_count), convergence_buffer.size());
    std::memcpy(convergence_buffer.data(), across.data(), count * sizeof(float));
}

float SimulationState::get_max_change() const {
    // Guard: only compare up to the lesser of dynamic_signals_count and
    // convergence_buffer.size() so we never read OOB if the buffer is smaller.
    size_t count = std::min(static_cast<size_t>(dynamic_signals_count), convergence_buffer.size());
    float max_change = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float change = std::abs(across[i] - convergence_buffer[i]);
        if (change > max_change) {
            max_change = change;
        }
    }
    return max_change;
}

bool SimulationState::has_converged(float tolerance) const {
    // Guard: only compare up to the lesser of dynamic_signals_count and
    // convergence_buffer.size() so we never read OOB if the buffer is smaller.
    size_t count = std::min(static_cast<size_t>(dynamic_signals_count), convergence_buffer.size());
    for (size_t i = 0; i < count; ++i) {
        float delta = std::abs(across[i] - convergence_buffer[i]);
        if (delta > tolerance) {
            return false;
        }
    }
    return true;
}
