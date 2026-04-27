#pragma once

#include "core/solvers/common/provider.h"

#include "../state.h"
#include "../../common/nodal_types.h"

/// CurrentSense - series current measurement node (ammeter)
/// Sits in series between v_in and v_out with near-zero resistance.
/// Outputs measured current (amperes) on i_out port.
template <typename Provider = JitProvider>
class CurrentSense {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    NodalPrimitiveHandle electrical_handle;
    float conductance = 1000.0f;  // high conductance = low series resistance

    CurrentSense() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
};
