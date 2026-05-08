#include "azs.h"
#include "core/solvers/common/port_names.h"
#include "../../common/nodal_types.h"
#include <cmath>

template <typename Provider>
void AZS<Provider>::pre_load() {
    if (i_nominal > 0.0f) {
        r_heat = 1.0f / (i_nominal * i_nominal);
    }
}

template <typename Provider>
void AZS<Provider>::execute(SimulationState& st, double dt) {
    // Electrical behavior is solver-owned via dynamic conductance branch.
    // Estimate branch current from solved electrical runtime for thermal model.
    if (st.electrical_rt != nullptr && is_valid(electrical_handle)) {
        current = std::fabs(get_branch_flow(*st.electrical_rt, electrical_handle));
    } else {
        current = 0.0f;
    }
    
    // Thermal behavior: use branch current from solver.
    // T += (I² * r_heat - T * k_cool) * dt
    float const I = current;
    temp += (I * I * r_heat - temp * k_cool) * dt;
    // Floor at zero
    temp = std::max(temp, 0.0);
}

template <typename Provider>
void AZS<Provider>::commit(SimulationState& st, double /*dt*/) {
    float const current_control = st.values[provider.get(PortNames::control)];
    if (std::abs(current_control - last_control) > 0.1f) {
        if (!closed) tripped = false;
        closed = !closed;
    }
    last_control = current_control;

    // Thermal trip
    if (closed && temp > 1.0f) {
        closed = false;
        tripped = true;
    }

    // Output state signals
    st.values[provider.get(PortNames::state)] = closed ? 1.0f : 0.0f;
    st.values[provider.get(PortNames::temp)] = temp;
    st.values[provider.get(PortNames::tripped)] = tripped ? 1.0f : 0.0f;
}

template class AZS<JitProvider>;
