#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// PneumaticRef — fixed pneumatic pressure reference (boundary condition).
///
/// Pneumatic analog of RefNode/PressureRef. Writes a constant pressure
/// value to its signal each frame. Typically set to atmospheric pressure
/// (101.325 kPa) as the pneumatic reference, or 0.0 for gauge pressure.
template <typename Provider = JitProvider>
class PneumaticRef {
public:
    static constexpr Domain domain = Domain::Pneumatic;

    Provider provider;
    float pressure = 0.0f;

    PneumaticRef() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
