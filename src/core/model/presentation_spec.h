#pragma once

#include "blueprint_v2/blueprint/node_content_type.h"
#include <string>
#include <optional>
#include <utility>
#include <unordered_map>

/// Editor-only presentation fields extracted from type definitions.
/// This separates UI/presentation concerns from the solver's ComponentSpec.
struct TypePresentation {
    std::string description;
    bp2::NodeContentType content_type = bp2::NodeContentType::None;
    std::string render_hint;
    std::optional<std::pair<float, float>> default_size;
    bool visual_only = false;
};

struct PresentationRegistry {
    std::unordered_map<std::string, TypePresentation> specs;

    [[nodiscard]] const TypePresentation* get(const std::string& classname) const {
        const auto it = specs.find(classname);
        if (it != specs.end()) return &it->second;
        return nullptr;
    }

    [[nodiscard]] bool has(const std::string& classname) const {
        return specs.contains(classname);
    }
};
