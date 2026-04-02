#pragma once

#include "provider.h"
#include "../state.h"
#include "../subsolvers/subsolver_types.h"
#include <cmath>

/// Battery - voltage source with internal resistance
template <typename Provider = JitProvider>
class Battery {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    ElectricalPrimitiveHandle electrical_handle;
    double capacity = 1000.0;
    double charge = 1000.0;
    float internal_r = 0.01f;
    float inv_internal_r = 100.0f;
    float v_nominal = 28.0f;

    Battery() = default;

    void pre_load() {
        float safe_r = std::max(internal_r, 1e-6f);
        inv_internal_r = 1.0f / safe_r;
    }

    void execute(SimulationState& /*st*/, double /*dt*/) {}

    void commit(SimulationState& st, double dt) {
        float discharge_current = 0.0f;
        if (st.electrical_rt != nullptr) {
            float i = get_branch_current(*st.electrical_rt, electrical_handle);
            discharge_current = std::max(0.0f, -i);
        }
        charge -= static_cast<double>(discharge_current) * static_cast<double>(dt) / 3600.0;
        if (charge < 0.0) charge = 0.0;
        if (charge > capacity) charge = capacity;

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
};
