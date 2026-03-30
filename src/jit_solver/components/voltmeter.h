#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Voltmeter - analog voltage gauge
template <typename Provider = JitProvider>
class Voltmeter {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float min = 0.0f;    // Gauge minimum display value
    float max = 28.0f;   // Gauge maximum display value

    Voltmeter() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
