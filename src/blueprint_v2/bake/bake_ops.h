#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/registry/type_registry.h"

namespace bp2 {

Blueprint bake_nested(Blueprint const& bp,
                      ui::InternedId nested_id,
                      TypeRegistry const& registry);

struct UnbakeResult {
    Blueprint blueprint;
    ui::InternedId referenced_id;
};

std::optional<UnbakeResult> try_unbake(Blueprint const& bp,
                                       ui::InternedId nested_id,
                                       TypeRegistry const& registry);

Blueprint bake_all(Blueprint const& bp,
                   TypeRegistry const& registry);

} // namespace bp2
