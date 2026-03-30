#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Merger - 2-to-1 signal merger (inverse of Splitter)
template <typename Provider = JitProvider>
class Merger {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    Merger() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
