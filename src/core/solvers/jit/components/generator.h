#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"
#include "../subsolvers/nodal_types.h"

/// Generator - voltage source like battery
template <typename Provider = JitProvider>
class Generator {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    NodalPrimitiveHandle electrical_handle;
    float internal_r = 0.005f;
    float inv_internal_r = 200.0f; // Precomputed
    float v_nominal = 28.5f;

    Generator() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
