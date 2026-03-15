#pragma once

#include "data/blueprint.h"
#include <optional>

/// Save blueprint to file (uses editor format).
[[nodiscard]] bool save_blueprint_to_file(const Blueprint& bp, const char* path);

/// Load blueprint from file.
[[nodiscard]] std::optional<Blueprint> load_blueprint_from_file(const char* path);
