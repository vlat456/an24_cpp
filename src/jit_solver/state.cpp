#include "state.h"

uint32_t SimulationState::allocate_signal(float initial_value, SignalType type) {
    if (type.is_fixed) {
        const uint32_t idx = static_cast<uint32_t>(values.size());
        values.push_back(initial_value);
        signal_types.push_back(type);
        return idx;
    }

    const uint32_t idx = dynamic_signals_count;
    values.insert(values.begin() + idx, initial_value);
    signal_types.insert(signal_types.begin() + idx, type);
    dynamic_signals_count++;
    return idx;
}
