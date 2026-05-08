#include "pneumatic_valve.h"
#include "core/solvers/common/port_names.h"

template <typename Provider>
void PneumaticValve<Provider>::execute(SimulationState& st, double /*dt*/) {
    // Read branch flow from pneumatic solver for diagnostics.
    if (st.pneumatic_rt != nullptr && is_valid(pneumatic_handle)) {
        flow = get_branch_flow(*st.pneumatic_rt, pneumatic_handle);
    } else {
        flow = 0.0f;
    }
}

template <typename Provider>
void PneumaticValve<Provider>::commit(SimulationState& st, double /*dt*/) {
    // Control logic: valve opens when ctrl exceeds 0.5.
    float const ctrl = st.values[provider.get(PortNames::ctrl)];
    bool const ctrl_active = ctrl > 0.5f;

    // Apply normally_closed logic: NC valve opens when ctrl is active.
    state = normally_closed ? ctrl_active : !ctrl_active;

    // Write state signal for BoolSwitch patch op (read next frame).
    st.values[provider.get(PortNames::state)] = state ? 1.0f : 0.0f;
}

template <typename Provider>
void PneumaticValve<Provider>::pre_load() {
    state = !normally_closed;
}

template class PneumaticValve<JitProvider>;
