#include "state.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace an24 {

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
    for (size_t i = 0; i < conductance.size(); ++i) {
        if (signal_types[i].is_fixed) {
            inv_conductance[i] = 0.0f;  // fixed signals: SOR won't update
        } else if (conductance[i] > 1e-9f) {
            inv_conductance[i] = 1.0f / conductance[i];
        } else {
            inv_conductance[i] = 0.0f;
        }
    }
}

void SimulationState::solve_signals_balance(float sor_omega) {
    for (size_t i = 0; i < across.size(); ++i) {
        if (!signal_types[i].is_fixed && inv_conductance[i] > 0.0f) {
            across[i] += through[i] * inv_conductance[i] * sor_omega;
        }
    }
}

void SimulationState::solve_signals_balance_fast(float inv_omega) {
    for (size_t i = 0; i < across.size(); ++i) {
        if (!signal_types[i].is_fixed && inv_conductance[i] > 0.0f) {
            across[i] += through[i] * inv_conductance[i] * inv_omega;
        }
    }
}

void SimulationState::save_convergence_state() {
    std::memcpy(convergence_buffer.data(), across.data(), across.size() * sizeof(float));
}

float SimulationState::get_max_change() const {
    float max_change = 0.0f;
    for (size_t i = 0; i < dynamic_signals_count; ++i) {
        float change = std::abs(across[i] - convergence_buffer[i]);
        if (change > max_change) {
            max_change = change;
        }
    }
    return max_change;
}

bool SimulationState::has_converged(float tolerance) const {
    for (uint32_t i = 0; i < dynamic_signals_count; ++i) {
        float delta = std::abs(across[i] - convergence_buffer[i]);
        if (delta > tolerance) {
            return false;
        }
    }
    return true;
}

} // namespace an24
