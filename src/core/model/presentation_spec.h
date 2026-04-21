#pragma once

#include <string>
#include <optional>
#include <utility>
#include <unordered_map>

/// Editor-only presentation fields extracted from type definitions.
/// This separates UI/presentation concerns from the solver's TypeDefinition.
struct TypePresentation {
    std::string description;
    std::string content_type = "None";
    std::string render_hint;
    std::optional<std::pair<float, float>> default_size;
};

struct PresentationRegistry {
    std::unordered_map<std::string, TypePresentation> specs;

    const TypePresentation* get(const std::string& classname) const {
        auto it = specs.find(classname);
        if (it != specs.end()) return &it->second;
        return nullptr;
    }

    bool has(const std::string& classname) const {
        return specs.count(classname) > 0;
    }
};
