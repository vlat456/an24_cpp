#include "inertia_node.h"
#include "core/solvers/common/port_registry.h"

template <typename Provider>
void InertiaNode<Provider>::execute(SimulationState& st, double dt) {
    const float torque_cmd = st.values[provider.get(PortNames::torque_in)];
    const float mass_param = st.values[provider.get(PortNames::mass)];
    const float inv_inertia_param = st.values[provider.get(PortNames::inv_inertia)];
    const float damping_param = st.values[provider.get(PortNames::damping)];

    const float net_torque = torque_cmd * mass_param - damping_param * rpm;
    const float accel = net_torque * inv_inertia_param;
    next_rpm = rpm + accel * dt;

    st.values[provider.get(PortNames::rpm_out)] = rpm;
}

template <typename Provider>
void InertiaNode<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
    rpm = next_rpm;
}

template <typename Provider>
void InertiaNode<Provider>::pre_load() {
    rpm = initial_rpm;
    next_rpm = initial_rpm;
}

template class InertiaNode<JitProvider>;
