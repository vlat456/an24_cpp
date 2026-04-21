#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>

#include "core/model/component_spec.h"
#include "core/model/resolved_device.h"
#include "core/model/device_instance.h"
#include "core/model/presentation_spec.h"
#include "core/model/catalog_registry.h"

struct ComponentRegistry {
    std::unordered_map<std::string, ComponentSpec> types;
    PresentationRegistry presentation;
    CatalogRegistry catalog;

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

    MenuTree build_menu_tree() const { return catalog.build_menu_tree(types, presentation); }
    std::optional<std::string> validate_instance(const DeviceInstance& instance) const;
    std::optional<std::string> validate_instance(const ResolvedDevice& instance) const;
    std::vector<std::string> get_composites_topo_sorted() const;
};
