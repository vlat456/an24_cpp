#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// AND - logical AND gate (o = A && B)
template <typename Provider = JitProvider>
class AND {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    AND() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
