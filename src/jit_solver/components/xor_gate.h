#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// XOR - logical XOR gate (o = A != B)
template <typename Provider = JitProvider>
class XOR {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    XOR() = default;

    void solve_logical(SimulationState& st, float dt);
    void execute(SimulationState& st, float dt);
    void pre_load() {}
};
