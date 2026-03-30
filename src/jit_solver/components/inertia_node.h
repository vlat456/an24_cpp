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
    float mass = 1.0f;
    float inv_mass = 1.0f;
    float damping = 0.5f;

    InertiaNode() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load();
};
