#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"

#include <optional>
#include <string_view>

namespace editor {

std::optional<std::string_view> select_slider_readback_port(const bp2::Blueprint::Node& node,
                                                            ui::StringInterner& interner);

} // namespace editor
