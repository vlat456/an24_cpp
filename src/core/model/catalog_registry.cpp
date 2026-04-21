#include "core/model/catalog_registry.h"

#include <algorithm>
#include <functional>

MenuTree CatalogRegistry::build_menu_tree(
    const std::unordered_map<std::string, ComponentSpec>& types,
    const PresentationRegistry& presentation) const
{
    MenuTree root;
    for (const auto& [classname, _] : types) {
        if (classname.empty()) {
            continue;
        }

        MenuTree* node = &root;
        auto cat_it = categories.find(classname);
        if (cat_it != categories.end() && !cat_it->second.empty()) {
            const std::string& cat = cat_it->second;
            size_t start = 0;
            while (start < cat.size()) {
                size_t slash = cat.find('/', start);
                std::string segment = slash == std::string::npos
                    ? cat.substr(start)
                    : cat.substr(start, slash - start);
                node = &node->children[segment];
                start = slash == std::string::npos ? cat.size() : slash + 1;
            }
        }

        node->entries.push_back(classname);
        const auto* pres = presentation.get(classname);
        node->labels[classname] = pres && !pres->description.empty()
            ? pres->description
            : classname;
    }

    std::function<void(MenuTree&)> sort_tree = [&](MenuTree& tree) {
        std::sort(tree.entries.begin(), tree.entries.end());
        for (auto& [_, child] : tree.children) {
            sort_tree(child);
        }
    };
    sort_tree(root);

    return root;
}