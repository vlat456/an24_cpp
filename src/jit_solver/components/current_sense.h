#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"
#include "../subsolvers/subsolver_types.h"

/// CurrentSense - series current measurement node (ammeter)
/// Sits in series between v_in and v_out with near-zero resistance.
/// Outputs measured current (amperes) on i_out port.
template <typename Provider = JitProvider>
class CurrentSense {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    ElectricalPrimitiveHandle electrical_handle;
    float conductance = 1000.0f;  // high conductance = low series resistance

    CurrentSense() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
