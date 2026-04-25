#include "current_sense.h"
#include "core/solvers/common/port_names.h"
#include "../state.h"

template <typename Provider>
void CurrentSense<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Read solved branch current from the electrical subsolver.
    // All upstream sources (Battery, Generator, ControlledVoltageSource) are now
    // solver-owned, so branch current is always computed by the solver.
    float i_out = 0.0f;
    if (st.electrical_rt != nullptr) {
        i_out = get_branch_current(*st.electrical_rt, electrical_handle);
    }

    st.values[provider.get(PortNames::i_out)] = i_out;
}

template <typename Provider>
void CurrentSense<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class CurrentSense<JitProvider>;
