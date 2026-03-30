#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"
#include <string>

/// BlueprintInput - input port marker for nested blueprints
template <typename Provider = JitProvider>
class BlueprintInput {
public:
    static constexpr Domain domain = Domain::Electrical;

    Provider provider;

    // Exposed port metadata (for parent blueprint)
    std::string exposed_type_str = "V";      // For type validation
    std::string exposed_direction_str = "In";  // For direction validation

    BlueprintInput() = default;

    void execute(SimulationState& st, float dt);
    void commit(SimulationState& st, float dt);
    void pre_load() {}
};
