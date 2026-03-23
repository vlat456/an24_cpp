#pragma once

#include "ui/core/interned_id.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include <unordered_map>
#include <optional>
#include <string>
#include <functional>

namespace bp2 {

class TypeRegistry {
public:
    struct Entry {
        ui::InternedId type_id;
        Interface iface;
        std::string description;
        bool is_blueprint = false;
        Blueprint const* blueprint = nullptr;
        std::unordered_map<std::string, std::string> param_defaults;
    };

    TypeRegistry() = default;

    void register_component(
        ui::InternedId type_id,
        Interface iface,
        std::string description = "",
        std::unordered_map<std::string, std::string> param_defaults = {});

    void register_blueprint(
        ui::InternedId type_id,
        Interface iface,
        std::string description = "",
        Blueprint const* bp = nullptr,
        std::unordered_map<std::string, std::string> param_defaults = {});

    Entry const* find(ui::InternedId type_id) const;
    bool has(ui::InternedId type_id) const;

    Interface const& interface_of(ui::InternedId type_id) const;

    void set_on_missing(std::function<void(ui::InternedId)> callback);
    Entry const* find_or_load(ui::InternedId type_id);

    size_t size() const { return entries_.size(); }

    auto begin() const { return entries_.begin(); }
    auto end() const { return entries_.end(); }

    static TypeRegistry create_test_registry(ui::StringInterner& interner);

private:
    std::unordered_map<ui::InternedId, Entry> entries_;
    std::function<void(ui::InternedId)> on_missing_;
};

} // namespace bp2
