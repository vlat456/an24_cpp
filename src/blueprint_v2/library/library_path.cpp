#include "library_path.h"

#include "json_parser/json_parser.h"

namespace bp2 {

std::optional<std::string> resolve_category_relative_blueprint_path(const TypeRegistry& registry,
                                                                    const std::string& blueprint_id) {
    if (!registry.has(blueprint_id)) {
        return std::nullopt;
    }

    std::string path;
    auto it = registry.categories.find(blueprint_id);
    if (it != registry.categories.end() && !it->second.empty()) {
        path += it->second;
        path += "/";
    }
    path += blueprint_id;
    return path;
}

std::optional<std::string> resolve_library_blueprint_path(const TypeRegistry& registry,
                                                          const std::string& blueprint_id) {
    auto relative = resolve_category_relative_blueprint_path(registry, blueprint_id);
    if (!relative.has_value()) {
        return std::nullopt;
    }

    std::string path = "library/";
    path += *relative;
    path += ".blueprint";
    return path;
}

} // namespace bp2
