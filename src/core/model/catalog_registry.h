#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>

#include "core/model/component_spec.h"
#include "core/model/presentation_spec.h"

struct MenuTree {
    std::vector<std::string> entries;
    std::unordered_map<std::string, std::string> labels;
    std::map<std::string, MenuTree> children;
};

struct CatalogRegistry {
    std::unordered_map<std::string, std::string> categories;
    MenuTree build_menu_tree(const std::unordered_map<std::string, ComponentSpec>& types,
                            const PresentationRegistry& presentation) const;
};