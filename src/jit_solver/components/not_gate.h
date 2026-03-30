#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// NOT - logical NOT gate (o = !A)
template <typename Provider = JitProvider>
class NOT {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    NOT() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
