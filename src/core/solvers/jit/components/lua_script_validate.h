#pragma once

#include <optional>
#include <string>
#include <string_view>

std::optional<std::string> lua_validate_script(std::string_view script);
