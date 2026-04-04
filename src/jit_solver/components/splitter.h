#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Splitter - 1-to-2 signal splitter
template <typename Provider = JitProvider>
class Splitter {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Splitter() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
