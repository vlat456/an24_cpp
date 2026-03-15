#pragma once

#include <string>
#include <optional>


/// File dialog wrappers - abstracts NFD library
namespace dialogs {

/// Open blueprint file dialog
std::optional<std::string> openBlueprint();

/// Save blueprint file dialog
std::optional<std::string> saveBlueprint(const std::string& defaultName = "blueprint.blueprint");

} // namespace dialogs

