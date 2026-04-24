#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"

/// Spring - mechanical spring-damper with preload
template <typename Provider = JitProvider>
class Spring {
public:
    static constexpr Domain domain = Domain::Mechanical;
    Provider provider;

    float k = 1000.0f;          // Stiffness (N/m)
    float c = 10.0f;            // Viscous damping coefficient (N*s/m)
    float rest_length = 0.1f;   // Free length
    bool compression_only = true;

    // State for velocity estimation (finite difference)
    float prev_delta_x = 0.0f;
    float first_frame_mask = 1.0f; // Branchless cold start

    Spring() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();

};
