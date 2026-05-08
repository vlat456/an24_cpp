#include "normalize.h"
#include "core/solvers/common/port_names.h"
#include <algorithm>
#include <cmath>

template <typename Provider>
void Normalize<Provider>::execute(SimulationState& st, double /*dt*/) {
    float const input = st.values[provider.get(PortNames::in)];
    float const lo = st.values[provider.get(PortNames::min)];
    float const hi = st.values[provider.get(PortNames::max)];

    float const range = hi - lo;
    float const inv_range = (std::abs(range) > 1e-6f) ? (1.0f / range) : 0.0f;

    float const normalized = (input - lo) * inv_range;
    st.values[provider.get(PortNames::out)] = std::clamp(normalized, 0.0f, 1.0f);
}

template <typename Provider>
void Normalize<Provider>::commit(SimulationState& st, double /*dt*/) {
    (void)st;
}

template class Normalize<JitProvider>;
