#pragma once

#include <string>
#include <vector>
#include <utility>

/// Core connection — used by simulation, signal allocation, JIT solver.
/// Only carries logical connectivity (from → to). No visual data.
struct Connection {
    std::string from;
    std::string to;
};

/// Extended connection with visual wire routing data.
/// Used by library loading (TypeDefinition → CompositeSpec) and
/// transferred to bp2::Blueprint::Wire via type_def_to_blueprint.
/// Implicitly converts to const Connection& where needed.
struct RoutedConnection : Connection {
    std::vector<std::pair<float, float>> routing_points;
};
