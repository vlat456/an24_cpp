#pragma once

#include <string>
#include <optional>

namespace an24 {

/// File dialog wrappers - abstracts NFD library
namespace dialogs {

/// Open blueprint file dialog
std::optional<std::string> openBlueprint();

/// Save blueprint file dialog
std::optional<std::string> saveBlueprint(const std::string& defaultName = "blueprint.blueprint");

} // namespace dialogs

} // namespace an24
