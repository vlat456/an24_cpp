#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// ElectricalSource — explicit primitive electrical voltage source (Thevenin).
///
/// Maps directly to the TheveninSource solver role.
/// No wrapper logic: all electrical behavior comes from the solver.
/// This is a "primitive-first" component, proving the architecture
/// supports composed systems without legacy wrapper classification.
template <typename Provider = JitProvider>
class ElectricalSource {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float voltage = 28.0f;
    float resistance = 0.01f;

    ElectricalSource() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
