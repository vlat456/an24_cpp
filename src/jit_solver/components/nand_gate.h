#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// NAND - logical NAND gate (o = !(A && B))
template <typename Provider = JitProvider>
class NAND {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    NAND() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
