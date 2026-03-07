#pragma once

#include "provider.h"
#include "../state.h"
#include <cstdint>

namespace an24 {

// =============================================================================
// Template Components with Provider Pattern
// =============================================================================

/// Battery with Provider - works with both AotProvider and JitProvider
template <typename Provider>
class Battery_Provider {
public:
    Provider provider;
    std::string name;
    float capacity = 1000.0f;
    float charge = 1000.0f;
    float internal_r = 0.01f;
    float inv_internal_r = 100.0f;
    float v_nominal = 28.0f;

    void solve_electrical(SimulationState& st, float dt) {
        // Provider lookup - AOT: compile-time const, JIT: runtime map lookup
        float v_gnd = st.across[provider.get(PortNames::v_in)];
        float v_bus = st.across[provider.get(PortNames::v_out)];
        float g = inv_internal_r;

        // Thevenin -> Norton: I = (V_nominal + V_gnd - V_bus) / R
        float i = (v_nominal + v_gnd - v_bus) * g;
        i = (i > 1000.0f) ? 1000.0f : ((i < -1000.0f) ? -1000.0f : i); // clamp

        st.through[provider.get(PortNames::v_in)] -= i;
        st.through[provider.get(PortNames::v_out)] += i;
    }

    void pre_load() {
        if (internal_r > 0.0f) {
            inv_internal_r = 1.0f / internal_r;
        } else {
            inv_internal_r = 0.0f;
        }
    }
};

/// Resistor with Provider
template <typename Provider>
class Resistor_Provider {
public:
    Provider provider;
    std::string name;
    float conductance = 0.1f;

    void solve_electrical(SimulationState& st, float dt) {
        float v_in = st.across[provider.get(PortNames::v_in)];
        float v_out = st.across[provider.get(PortNames::v_out)];
        float g = conductance;

        float i = (v_in - v_out) * g;
        st.through[provider.get(PortNames::v_in)] -= i;
        st.through[provider.get(PortNames::v_out)] += i;
    }
};

/// Load with Provider (single port to ground)
template <typename Provider>
class Load_Provider {
public:
    Provider provider;
    std::string name;
    float conductance = 0.1f;

    void solve_electrical(SimulationState& st, float dt) {
        float v = st.across[provider.get(PortNames::input)];
        float g = conductance;

        float i = v * g;
        st.through[provider.get(PortNames::input)] += i;
    }
};

/// Comparator with Provider (logical domain)
template <typename Provider>
class Comparator_Provider {
public:
    Provider provider;
    std::string name;
    float Von = 0.1f;
    float Voff = -0.1f;
    bool output_state = false;

    void solve_logical(SimulationState& st, float dt) {
        float Va = st.across[provider.get(PortNames::Va)];
        float Vb = st.across[provider.get(PortNames::Vb)];
        float diff = Va - Vb;

        // Branchless hysteresis
        bool set = (diff >= Von);
        bool keep = (diff > Voff);
        output_state = set || (output_state && keep);

        st.across[provider.get(PortNames::o)] = output_state ? 1.0f : 0.0f;
    }
};

} // namespace an24
