#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"
#include "../../common/nodal_types.h"

/// PneumaticCompressor — RPM-driven pneumatic pressure source.
///
/// Solver-owned: the nodal subsolver computes actual pressure at the
/// p_out node (accounting for load). This component handles:
///   - execute(): computes output pressure from RPM, writes to p_source signal
///   - commit(): no state transitions needed
///
/// Physics: output pressure is proportional to RPM² (centrifugal compressor
/// characteristic). The CopySignal patch op reads p_source each frame and
/// copies it to the PressureSource element's value_a (Thevenin P_th).
template <typename Provider = JitProvider>
class PneumaticCompressor {
public:
    static constexpr Domain domain = Domain::Pneumatic;

    Provider provider;
    NodalPrimitiveHandle pneumatic_handle;
    float max_pressure = 700.0f;     ///< Max output pressure (kPa, ~7 atm)
    float internal_r = 0.05f;        ///< Internal flow resistance (kPa·s/L)
    float rated_rpm = 24000.0f;      ///< RPM at which max_pressure is reached

    PneumaticCompressor() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
