#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// InertiaNode - mechanical inertia
template <typename Provider = JitProvider>
class InertiaNode {
public:
    static constexpr Domain domain = Domain::Mechanical;

    Provider provider;
    float initial_rpm = 1.0f;
    double rpm = 1.0;
    double next_rpm = 1.0;

    InertiaNode() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
