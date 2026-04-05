#include "state.h"

uint32_t SimulationState::allocate_signal(float initial_value, SignalType type) {
    const uint32_t idx = static_cast<uint32_t>(values.size());
    values.push_back(initial_value);
    signal_types.push_back(type);
    if (!type.is_fixed) {
        dynamic_signals_count++;
    }
    return idx;
}
