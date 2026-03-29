#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// RUG82 - Coal column voltage regulator
template <typename Provider = JitProvider>
class RUG82 {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float v_target = 28.5f;
    float k_mod = 0.5f;
    float next_k_mod = 0.5f;
    float kp = 2.0f;

    RUG82() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() { next_k_mod = k_mod; }
};
