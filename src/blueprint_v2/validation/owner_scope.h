#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include <optional>
#include <string>

namespace bp2 {

/// Validate the strict structural owner_scope contract.
/// Empty owner_scope means root scope. Non-empty owner_scope must resolve to
/// an existing expandable host node with an embedded Nested definition.
std::optional<std::string> validate_owner_scope_reference(const Blueprint& bp,
                                                          const Blueprint::Node& node,
                                                          const ui::StringInterner& interner);

} // namespace bp2
