#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"
#include "../../common/nodal_types.h"

/// PneumaticValve — controllable pneumatic flow valve.
///
/// Solver-owned: the nodal subsolver computes actual pressures and flow.
/// BoolSwitch patch op reads the state signal each frame and sets
/// element_value_a to g_open or g_closed accordingly.
///
/// Follows the same timing pattern as SolenoidValve:
///   - execute(): reads branch flow from solver (diagnostics)
///   - commit(): reads ctrl signal, updates state, writes state signal
/// One-frame delay: state change in commit() takes effect in next frame's patch op.
template <typename Provider = JitProvider>
class PneumaticValve {
public:
    static constexpr Domain domain = Domain::Pneumatic;

    Provider provider;
    NodalPrimitiveHandle pneumatic_handle;
    float g_open = 5.0f;       ///< Conductance when valve is open (L/(s·kPa))
    float g_closed = 0.0001f;  ///< Conductance when valve is closed
    float flow = 0.0f;         ///< Branch flow from solver (L/s, diagnostic)
    bool normally_closed = true;
    bool state = false;         ///< Current valve state (true=open)

    PneumaticValve() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
