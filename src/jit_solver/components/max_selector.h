#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"

/// MaxSelector - outputs max(a, b).
///
/// Note: runtime currently aliases classname "MaxSelector" to Max<JitProvider>
/// in build_systems_dev for variant compatibility. This class is kept as the
/// long-term dedicated implementation target.
template <typename Provider = JitProvider>
class MaxSelector {
public:
    static constexpr Domain domain = Domain::Logical;

    Provider provider;

    MaxSelector() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st);
    void pre_load() {}
};
