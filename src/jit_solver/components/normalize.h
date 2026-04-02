#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// Normalize - maps [min..max] range to [0..1], result clamped
template <typename Provider = JitProvider>
class Normalize {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    float min = 0.0f;
    float max = 100.0f;
    float inv_range = 0.01f;  // precomputed in pre_load()

    Normalize() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
