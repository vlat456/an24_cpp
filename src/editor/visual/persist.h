#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <optional>

namespace ui { class StringInterner; }

/// Save blueprint to file using bp2 codec.
[[nodiscard]] bool save_blueprint_to_file(const bp2::Blueprint& bp,
                                           ui::StringInterner const& interner,
                                           bp2::PathArena const& arena,
                                           const char* path);

/// Load blueprint from file using bp2 codec.
[[nodiscard]] std::optional<bp2::Blueprint> load_blueprint_from_file(
    const char* path,
    ui::StringInterner& interner,
    bp2::PathArena& arena);
