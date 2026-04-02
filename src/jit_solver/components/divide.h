#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Divide - divider (o = A / B, returns 0 if B is 0)
template <typename Provider = JitProvider>
class Divide {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Divide() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
