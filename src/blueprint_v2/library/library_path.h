#pragma once

#include <optional>
#include <string>

struct TypeRegistry;

namespace bp2 {

std::optional<std::string> resolve_category_relative_blueprint_path(const TypeRegistry& registry,
                                                                    const std::string& blueprint_id);
std::optional<std::string> resolve_library_blueprint_path(const TypeRegistry& registry,
                                                          const std::string& blueprint_id);

} // namespace bp2
