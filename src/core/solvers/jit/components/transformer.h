#pragma once

#include "core/solvers/common/provider.h"
#include "component_enums.h"
#include "../state.h"

/// Transformer - AC transformer with voltage ratio
template <typename Provider = JitProvider>
class Transformer {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;
    float ratio = 1.0f;

    Transformer() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load() {}
};
