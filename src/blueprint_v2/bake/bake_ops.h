#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/library/blueprint_library.h"

namespace bp2 {

struct UnbakeResult {
    Blueprint blueprint;
    core::InternedId referenced_id;
};

/// Bake a single referenced blueprint-instance node into an embedded source.
/// Throws std::runtime_error if the target is missing, not a blueprint
/// instance, already embedded, or references an unknown blueprint.
Blueprint bake_node_blueprint_instance(Blueprint const& bp,
                                       core::InternedId node_id,
                                       BlueprintLibrary const& library);

std::optional<UnbakeResult> try_unbake(Blueprint const& bp,
                                       core::InternedId node_id,
                                       BlueprintLibrary const& library);

Blueprint bake_all(Blueprint const& bp,
                   BlueprintLibrary const& library);

} // namespace bp2
