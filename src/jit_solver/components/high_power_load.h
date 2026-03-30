#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// HighPowerLoad - high power electrical load (branchless, optimized)
template <typename Provider = JitProvider>
class HighPowerLoad {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float power_draw = 500.0f;
    float min_voltage_diff = 0.01f; // Minimum voltage diff to conduct

    HighPowerLoad() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
