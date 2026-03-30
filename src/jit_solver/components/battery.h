#pragma once

#include "provider.h"
#include "../state.h"
#include "../subsolvers/subsolver_types.h"

/// Battery - voltage source with internal resistance
template <typename Provider = JitProvider>
class Battery {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    ElectricalPrimitiveHandle electrical_handle;
    double capacity = 1000.0;   // Ah — double required: running accumulator
    double charge = 1000.0;     // Ah — double required: per-step delta can be
                                // below float32 ULP at large charge values
    float internal_r = 0.01f;
    float inv_internal_r = 100.0f;
    float v_nominal = 28.0f;

    Battery() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load();
};
