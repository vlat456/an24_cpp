#pragma once

#include "data/blueprint.h"
#include <string>
#include <optional>

/// Сериализация Blueprint в JSON строку
std::string blueprint_to_json(const Blueprint& bp);

/// Десериализация Blueprint из JSON строки
std::optional<Blueprint> blueprint_from_json(const std::string& json);

/// Сохранение в файл
bool save_blueprint_to_file(const Blueprint& bp, const char* path);

/// Загрузка из файла
std::optional<Blueprint> load_blueprint_from_file(const char* path);
