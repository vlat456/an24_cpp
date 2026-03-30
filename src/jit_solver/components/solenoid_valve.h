#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// SolenoidValve - electrically controlled hydraulic valve (branchless)
template <typename Provider = JitProvider>
class SolenoidValve {
public:
    static constexpr Domain domain = Domain::Hydraulic;

    Provider provider;
    bool normally_closed = true;
    float open_mask = 0.0f; // Branchless state (0.0 = closed, 1.0 = open)

    SolenoidValve() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
