#include "variable_conductance.h"
#include "port_registry.h"
#include "../state.h"

/// Execute: no-op. VariableConductance is solver-owned — node voltages (v_in, v_out)
/// are computed by the electrical subsolver, not pushed.  The cmd input is read by
/// update_dynamic_sources() which patches the conductance in the electrical plan
/// before each solve (one-frame-delay semantic).
template <typename Provider>
void VariableConductance<Provider>::execute(SimulationState& /*st*/, double /*dt*/) {
}

template <typename Provider>
void VariableConductance<Provider>::commit(SimulationState& /*st*/, double /*dt*/) {
}

template class VariableConductance<JitProvider>;

