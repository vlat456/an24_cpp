#pragma once

#include "data/blueprint.h"
#include <string>
#include <optional>

/// Сериализация Blueprint в JSON строку (simulator format: rewrites wires, skips Blueprint nodes)
std::string blueprint_to_json(const Blueprint& bp);

/// Editor save: serializes ALL nodes (flat, including Blueprint kind) + wires as-is
std::string blueprint_to_editor_json(const Blueprint& bp);

/// Десериализация Blueprint из JSON строки (handles both editor and simulator formats)
std::optional<Blueprint> blueprint_from_json(const std::string& json);

/// Сохранение в файл (uses editor format)
[[nodiscard]] bool save_blueprint_to_file(const Blueprint& bp, const char* path);

/// Загрузка из файла
[[nodiscard]] std::optional<Blueprint> load_blueprint_from_file(const char* path);
