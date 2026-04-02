#pragma once

#include "ui/core/interned_id.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include <unordered_map>
#include <optional>
#include <string>
#include <functional>
#include <vector>

namespace bp2 {

class TypeRegistry {
public:
    enum class ParamKind {
        Number,
        String,
        Bool,
        Enum,
        Table,
        Vec2,
    };

    struct ParamDescriptor {
        ParamKind kind = ParamKind::String;
        std::string default_value;
        std::vector<std::string> enum_values;
    };

    struct Entry {
        struct PortMeta {
            PortType type = PortType::Any;
            bool source_writer = false;
        };

        ui::InternedId type_id;
        Interface iface;
        std::string description;
        bool is_blueprint = false;
        Blueprint const* blueprint = nullptr;
        bool scheduler_source = false;
        std::vector<Domain> domains;
        std::unordered_map<std::string, std::string> param_defaults;
        std::unordered_map<std::string, ParamDescriptor> param_descriptors;
        std::unordered_map<std::string, PortMeta> port_meta;
    };

    TypeRegistry() = default;

    void register_component(
        ui::InternedId type_id,
        Interface iface,
        std::string description = "",
        std::unordered_map<std::string, std::string> param_defaults = {},
        std::unordered_map<std::string, ParamDescriptor> param_descriptors = {},
        bool scheduler_source = false,
        std::vector<Domain> domains = {},
        std::unordered_map<std::string, Entry::PortMeta> port_meta = {});

    void register_blueprint(
        ui::InternedId type_id,
        Interface iface,
        std::string description = "",
        Blueprint const* bp = nullptr,
        std::unordered_map<std::string, std::string> param_defaults = {},
        std::unordered_map<std::string, ParamDescriptor> param_descriptors = {},
        bool scheduler_source = false,
        std::vector<Domain> domains = {},
        std::unordered_map<std::string, Entry::PortMeta> port_meta = {});

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
