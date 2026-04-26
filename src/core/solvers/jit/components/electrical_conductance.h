#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"

/// ElectricalConductance — explicit primitive electrical element.
///
/// Maps directly to the ConductanceBranch solver role.
/// No wrapper logic: all electrical behavior comes from the solver.
/// This is the first "primitive-first" component, proving the architecture
/// supports composed systems without legacy wrapper classification.
template <typename Provider = JitProvider>
class ElectricalConductance {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float conductance = 0.1f;

    ElectricalConductance() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
