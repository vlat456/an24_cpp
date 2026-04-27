#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"
#include "../../common/nodal_types.h"

/// FuelTank — aircraft fuel reservoir with gravity head pressure.
///
/// Solver-owned: the hydraulic subsolver computes actual pressure at the
/// flow_out node (accounting for load). This component handles:
///   - execute(): computes gravity pressure, writes to p_source signal
///   - commit(): updates fuel level from consumption
///
/// The CopySignal patch op reads p_source each frame and copies it to the
/// PressureSource element's value_a (Thevenin P_th). One-frame delay is
/// consistent with the electrical solver-owned pattern.
template <typename Provider = JitProvider>
class FuelTank {
public:
    static constexpr Domain domain = Domain::Hydraulic;

    Provider provider;
    NodalPrimitiveHandle hydraulic_handle;
    float capacity = 1000.0f;       ///< Tank capacity (liters)
    double level = 1000.0;          ///< Current fuel amount (liters, state)
    double next_level = 1000.0;     ///< Next-frame fuel amount (liters)
    float density = 0.78f;          ///< Fuel density (kg/L, kerosene TS-1)
    float consumption_rate = 0.0f;  ///< Constant fuel draw (liters/second)
    float inv_capacity = 0.001f;    ///< Precomputed 1/capacity
    float internal_r = 0.1f;        ///< Hydraulic resistance of tank outlet (kPa·s/L)
    float tank_height = 1.0f;       ///< Tank physical height (meters) for gravity head

    static constexpr float GRAVITY = 9.81f;  ///< m/s²

    FuelTank() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
