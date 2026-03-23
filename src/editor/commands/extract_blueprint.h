#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <optional>
#include <string>
#include <vector>

namespace editor::commands {

std::optional<bp2::Blueprint> build_extracted_blueprint_atomic(
    const bp2::Blueprint& source,
    const std::vector<ui::InternedId>& selected_node_ids,
    const std::string& blueprint_name,
    const std::string& group_id,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    std::string* error_out = nullptr);

} // namespace editor::commands
