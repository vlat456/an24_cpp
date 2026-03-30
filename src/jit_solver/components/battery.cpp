#include "battery.h"
#include "port_registry.h"
#include <cmath>

template <typename Provider>
void Battery<Provider>::pre_load() {
    float safe_r = std::max(internal_r, 1e-6f);
    inv_internal_r = 1.0f / safe_r;
}

template <typename Provider>
void Battery<Provider>::execute(SimulationState& /*st*/, float /*dt*/) {
    // No-op: Battery is solver-owned. Electrical propagation runs via the
    // conductance matrix in the electrical subsolver, not via push scheduler.
}

template <typename Provider>
void Battery<Provider>::commit(SimulationState& st, float dt) {
    // NOTE: Battery is solver-owned (not in push scheduler sources/consumers).
    // This commit() is called explicitly via commit_solver_owned_devices() in
    // Simulator::step, AFTER the electrical solver computes branch currents.
    //
    // Discharge calculation using solved branch current.
    // TheveninSource branch current convention (from electrical_subsolver):
    //   branch_current = g*(Va - Vb) - In
    // where node_a=v_out(positive), node_b=v_in(negative), In=Vth*g.
    // When discharging, current exits node_a externally, meaning internal
    // a->b flow is negative. Therefore: discharge_current = max(0, -i).
    float discharge_current = 0.0f;
    if (st.electrical_rt != nullptr) {
        float i = get_branch_current(*st.electrical_rt, electrical_handle);
        discharge_current = std::max(0.0f, -i);
    }
    // Accumulate in double to avoid float32 ULP swallowing small deltas.
    // At charge=1000, float32 ULP is ~6.1e-5; typical delta is ~1.2e-5.
    charge -= static_cast<double>(discharge_current) * static_cast<double>(dt) / 3600.0;
    if (charge < 0.0) charge = 0.0;
    if (charge > capacity) charge = capacity;

    // Optional live telemetry outputs for editor/runtime inspection.
    if (provider.has(PortNames::charge_out)) {
        st.values[provider.get(PortNames::charge_out)] = static_cast<float>(charge);
    }
    if (provider.has(PortNames::soc_out)) {
        float soc = 0.0f;
        if (capacity > 0.0) {
            soc = static_cast<float>(charge / capacity);
        }
        st.values[provider.get(PortNames::soc_out)] = soc;
    }
}

template class Battery<JitProvider>;
