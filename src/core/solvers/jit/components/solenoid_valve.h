#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"
#include "../subsolvers/hydraulic_subsolver_types.h"

/// SolenoidValve — electrically controlled hydraulic valve.
///
/// Solver-owned: the hydraulic subsolver computes pressures at flow_in and
/// flow_out nodes. This component handles control logic only:
///   - execute(): reads branch flow from solver for diagnostics
///   - commit(): processes ctrl signal, writes open/closed state to signal
///
/// The BoolSwitch patch op reads the `state` signal each frame and switches
/// between g_open (valve open) and g_closed (valve closed) in the solver's
/// conductance matrix.
template <typename Provider = JitProvider>
class SolenoidValve {
public:
    static constexpr Domain domain = Domain::Hydraulic;

    Provider provider;
    HydraulicPrimitiveHandle hydraulic_handle;
    bool normally_closed = true;
    bool open = false;              ///< Committed valve state
    float flow = 0.0f;             ///< Branch flow from solver (L/s)
    float g_open = 10.0f;          ///< Hydraulic conductance when open (L/(s·kPa))
    float g_closed = 1e-4f;        ///< Hydraulic conductance when closed (L/(s·kPa))

    SolenoidValve() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
