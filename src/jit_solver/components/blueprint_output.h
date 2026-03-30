#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"
#include <string>

/// BlueprintOutput - output port marker for nested blueprints
template <typename Provider = JitProvider>
class BlueprintOutput {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;

    // Exposed port metadata (for parent blueprint)
    std::string exposed_type_str = "V";      // For type validation
    std::string exposed_direction_str = "Out";  // For direction validation

    BlueprintOutput() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
