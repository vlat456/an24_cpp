#pragma once

#include "data/blueprint.h"
#include <optional>

/// Сохранение в файл (uses editor format)
[[nodiscard]] bool save_blueprint_to_file(const Blueprint& bp, const char* path);

/// Загрузка из файла
[[nodiscard]] std::optional<Blueprint> load_blueprint_from_file(const char* path);
