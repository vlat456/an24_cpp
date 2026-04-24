#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"
#include "../subsolvers/subsolver_types.h"

/// AZS (Avtomatom Zaashchity Seti) - Aircraft circuit breaker
/// Hybrid switch + thermal fuse. Manual toggle via control port.
/// Thermal model: T += (I^2 * r_heat - T * k_cool) * dt, trips at T > 1.0
template <typename Provider = JitProvider>
class AZS {
public:
    static constexpr Domain domain = Domain::Electrical | Domain::Thermal;

    Provider provider;
    ElectricalPrimitiveHandle electrical_handle;
    bool closed = false;
    bool tripped = false;
    float last_control = 0.0f;
    double temp = 0.0;
    float current = 0.0f;
    float i_nominal = 20.0f;
    float g_open = 1e-6f;
    float g_closed = 1000.0f;
    float r_heat = 0.0025f;   // 1/(i_nominal^2) - precomputed
    float k_cool = 1.0f;

    AZS() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
