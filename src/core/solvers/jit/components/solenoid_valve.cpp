#include "solenoid_valve.h"
#include "core/solvers/common/port_names.h"
#include <cmath>

template <typename Provider>
void SolenoidValve<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Read branch flow from hydraulic solver for diagnostics.
    if (st.hydraulic_rt != nullptr && is_valid(hydraulic_handle)) {
        flow = get_branch_flow(*st.hydraulic_rt, hydraulic_handle);
    } else {
        flow = 0.0f;
    }
}

template <typename Provider>
void SolenoidValve<Provider>::commit(SimulationState& st, double /*dt*/) {
    // Control logic: valve opens when ctrl voltage exceeds 12V.
    float ctrl = st.values[provider.get(PortNames::ctrl)];
    bool ctrl_active = ctrl > 12.0f;

    // Apply normally_closed logic: NC valve opens when ctrl is active.
    open = normally_closed ? ctrl_active : !ctrl_active;

    // Write state signal for BoolSwitch patch op (read next frame).
    st.values[provider.get(PortNames::state)] = open ? 1.0f : 0.0f;
}

template class SolenoidValve<JitProvider>;
