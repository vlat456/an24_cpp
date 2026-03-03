#include "state.h"
#include <stdexcept>

namespace an24 {

uint32_t SimulationState::allocate_signal(float initial_value, SignalType type) {
    uint32_t idx = static_cast<uint32_t>(across.size());
    across.push_back(initial_value);
    through.push_back(0.0f);
    conductance.push_back(0.0f);
    inv_conductance.push_back(0.0f);
    signal_types.push_back(type);
    return idx;
}

void SimulationState::clear_through() {
    std::fill(through.begin(), through.end(), 0.0f);
    std::fill(conductance.begin(), conductance.end(), 0.0f);
}

void SimulationState::precompute_inv_conductance() {
    for (size_t i = 0; i < conductance.size(); ++i) {
        if (signal_types[i].is_fixed) {
            // Fixed signals: infinite conductance
            inv_conductance[i] = 1.0f;
        } else if (conductance[i] > 0.0f) {
            inv_conductance[i] = 1.0f / conductance[i];
        } else {
            // No conductance: open circuit
            inv_conductance[i] = 0.0f;
        }
    }
}

} // namespace an24
