#include "variable_conductance.h"
#include "port_registry.h"
#include "../state.h"
#include <cmath>

/// Push model implementation:
/// Reads cmd [0..1] control input, computes conductance g = lerp(g_min, g_max, cmd),
/// then passes v_in through to v_out as a simple deterministic behavior.
/// In push model without matrix solve, we do simple pass-through.
template <typename Provider>
void VariableConductance<Provider>::solve_electrical(SimulationState& st, float /*dt*/) {
    // Read control input (expected 0..1 range)
    float cmd = st.values[provider.get(PortNames::cmd)];
    
    // Clamp cmd to valid range and compute conductance
    float t = std::clamp(cmd, 0.0f, 1.0f);
    float g = g_min + (g_max - g_min) * t;
    
    // Push model: simple pass-through from v_in to v_out
    // Note: actual voltage division would require network solve,
    // but for deterministic push behavior we propagate input directly
    (void)g; // conductance available for future enhancement
    float v_in = st.values[provider.get(PortNames::v_in)];
    st.values[provider.get(PortNames::v_out)] = v_in;
}

/// Execute method for scheduler integration
template <typename Provider>
void VariableConductance<Provider>::execute(SimulationState& st, float dt) {
    solve_electrical(st, dt);
}

template class VariableConductance<JitProvider>;

