#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <cassert>

#include "core/model/component_spec.h"
#include "core/model/resolved_device.h"
#include "core/model/device_instance.h"
#include "core/model/presentation_spec.h"
#include "core/model/catalog_registry.h"

struct ComponentRegistry {
    // == Public data (will be made private after test migration in #235-B) ==
    std::unordered_map<std::string, ComponentSpec> types;
    PresentationRegistry presentation;
    CatalogRegistry catalog;

    // == Registration ==

    /// Register a component type with optional presentation and category.
    /// Asserts in debug if the spec's internal classname doesn't match the key.
    /// Overwrites any existing entry for the same classname.
    void register_type(
        const std::string& classname,
        ComponentSpec spec,
        TypePresentation pres = {},
        std::string category = "")
    {
        assert(spec_classname(spec) == classname && "classname key must match spec's internal classname");
        types[classname] = std::move(spec);
        presentation.specs[classname] = std::move(pres);
        if (!category.empty()) {
            catalog.categories[classname] = std::move(category);
        }
    }

    // == Lookup ==

    const ComponentSpec* get(const std::string& classname) const {
        auto it = types.find(classname);
        if (it != types.end()) return &it->second;
        return nullptr;
    }

    bool has(const std::string& classname) const {
        return types.count(classname) > 0;
    }

    std::vector<std::string> list_classnames() const {
        std::vector<std::string> names;
        names.reserve(types.size());
        for (const auto& [name, _] : types) names.push_back(name);
        return names;
    }

    // == Bulk read access ==

    /// Read-only view of all registered types.
    const std::unordered_map<std::string, ComponentSpec>& all_types() const { return types; }

    /// Read-only view of all presentations.
    const std::unordered_map<std::string, TypePresentation>& all_presentations() const {
        return presentation.specs;
    }

    /// Read-only view of all categories.
    const std::unordered_map<std::string, std::string>& all_categories() const {
        return catalog.categories;
    }

    // == Presentation delegation ==

    const TypePresentation* get_presentation(const std::string& classname) const {
        return presentation.get(classname);
    }

    // == Mutable presentation access for test configuration ==
    // Transitional: prefer constructing TypePresentation fully before register_type().
    TypePresentation& presentation_mut(const std::string& classname) {
        return presentation.specs[classname];
    }

    // == Catalog lookup ==

    const std::string* get_category(const std::string& classname) const {
        auto it = catalog.categories.find(classname);
        return it != catalog.categories.end() ? &it->second : nullptr;
    }

    // == Existing methods ==

    MenuTree build_menu_tree() const { return catalog.build_menu_tree(types, presentation); }
    std::optional<std::string> validate_instance(const DeviceInstance& instance) const;
    std::optional<std::string> validate_instance(const ResolvedDevice& instance) const;
    std::vector<std::string> get_composites_topo_sorted() const;
};
