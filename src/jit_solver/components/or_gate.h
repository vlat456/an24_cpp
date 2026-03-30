#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// OR - logical OR gate (o = A || B)
template <typename Provider = JitProvider>
class OR {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    OR() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
