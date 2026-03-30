#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// AZS (Avtomatom Zaashchity Seti) - Aircraft circuit breaker
/// Hybrid switch + thermal fuse. Manual toggle via control port.
/// Thermal model: T += (I^2 * r_heat - T * k_cool) * dt, trips at T > 1.0
template <typename Provider = JitProvider>
class AZS {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Thermal;

    Provider provider;
    bool closed = false;
    bool tripped = false;
    float last_control = 0.0f;
    float temp = 0.0f;
    float current = 0.0f;
    float i_nominal = 20.0f;
    float r_heat = 0.0025f;   // 1/(i_nominal^2) - precomputed
    float k_cool = 1.0f;

    AZS() = default;

    void commit_control(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load();
};
