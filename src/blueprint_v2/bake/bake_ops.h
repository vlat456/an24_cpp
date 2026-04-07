#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/library/blueprint_library.h"

namespace bp2 {

Blueprint bake_nested(Blueprint const& bp,
                      ui::InternedId nested_id,
                      BlueprintLibrary const& library);

struct UnbakeResult {
    Blueprint blueprint;
    ui::InternedId referenced_id;
};

std::optional<UnbakeResult> try_unbake(Blueprint const& bp,
                                       ui::InternedId nested_id,
                                       BlueprintLibrary const& library);

Blueprint bake_all(Blueprint const& bp,
                   BlueprintLibrary const& library);

} // namespace bp2
