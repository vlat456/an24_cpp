#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include <optional>
#include <string>

namespace bp2 {

std::optional<std::string> validate_owner_scope_reference(const Blueprint& bp,
                                                          const Blueprint::Node& node,
                                                          ui::StringInterner& interner);

} // namespace bp2
