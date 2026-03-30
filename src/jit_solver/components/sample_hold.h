#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// SampleHold - samples input on trigger rising edge and holds value
template <typename Provider = JitProvider>
class SampleHold {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    // Committed state fields
    float stored_value = 0.0f;
    float last_trig = 0.0f;

    // Staged next-state fields
    float next_stored_value = 0.0f;
    float next_last_trig = 0.0f;

    SampleHold() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
