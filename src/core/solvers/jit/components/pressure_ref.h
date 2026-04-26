#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// PressureRef — fixed hydraulic pressure reference (boundary condition).
///
/// Hydraulic analog of RefNode. Writes a constant pressure value to its signal
/// each frame. The build pipeline also extracts it as a FixedPressureNode for
/// the hydraulic subsolver, which stamps it as a boundary condition in the
/// nodal analysis.
template <typename Provider = JitProvider>
class PressureRef {
public:
    static constexpr Domain domain = Domain::Hydraulic;

    Provider provider;
    float pressure = 0.0f;

    PressureRef() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
