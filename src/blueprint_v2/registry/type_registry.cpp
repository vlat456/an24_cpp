#include "type_registry.h"
#include "ui/core/interned_id.h"
#include <stdexcept>

namespace bp2 {

void TypeRegistry::register_component(
    ui::InternedId type_id,
    Interface iface,
    std::string description,
    std::unordered_map<std::string, std::string> param_defaults,
    std::unordered_map<std::string, ParamDescriptor> param_descriptors) {
    Entry entry;
    entry.type_id = type_id;
    entry.iface = std::move(iface);
    entry.description = std::move(description);
    entry.is_blueprint = false;
    entry.param_defaults = std::move(param_defaults);
    entry.param_descriptors = std::move(param_descriptors);
    entries_[type_id] = std::move(entry);
}

void TypeRegistry::register_blueprint(
    ui::InternedId type_id,
    Interface iface,
    std::string description,
    Blueprint const* bp,
    std::unordered_map<std::string, std::string> param_defaults,
    std::unordered_map<std::string, ParamDescriptor> param_descriptors) {
    Entry entry;
    entry.type_id = type_id;
    entry.iface = std::move(iface);
    entry.description = std::move(description);
    entry.is_blueprint = true;
    entry.blueprint = bp;
    entry.param_defaults = std::move(param_defaults);
    entry.param_descriptors = std::move(param_descriptors);
    entries_[type_id] = std::move(entry);
}

TypeRegistry::Entry const* TypeRegistry::find(ui::InternedId type_id) const {
    auto it = entries_.find(type_id);
    if (it == entries_.end()) return nullptr;
    return &it->second;
}

bool TypeRegistry::has(ui::InternedId type_id) const {
    return entries_.count(type_id) > 0;
}

Interface const& TypeRegistry::interface_of(ui::InternedId type_id) const {
    auto* entry = find(type_id);
    if (!entry) {
        throw std::runtime_error("TypeRegistry: unknown type");
    }
    return entry->iface;
}

void TypeRegistry::set_on_missing(std::function<void(ui::InternedId)> callback) {
    on_missing_ = std::move(callback);
}

TypeRegistry::Entry const* TypeRegistry::find_or_load(ui::InternedId type_id) {
    auto* entry = find(type_id);
    if (entry) return entry;

    if (on_missing_) {
        on_missing_(type_id);
        return find(type_id);
    }
    return nullptr;
}

TypeRegistry TypeRegistry::create_test_registry(ui::StringInterner& interner) {
    TypeRegistry reg;

    reg.register_component(
        interner.intern("Battery"),
        Interface({
            {interner.intern("v_in"), Domain::Electrical, Direction::Input},
            {interner.intern("v_out"), Domain::Electrical, Direction::Output},
        }),
        "DC battery source"
    );

    reg.register_component(
        interner.intern("Resistor"),
        Interface({
            {interner.intern("in"), Domain::Electrical, Direction::Input},
            {interner.intern("out"), Domain::Electrical, Direction::Output},
        }),
        "Resistor"
    );

    reg.register_component(
        interner.intern("Ground"),
        Interface({
            {interner.intern("gnd"), Domain::Electrical, Direction::InOut},
        }),
        "Ground reference"
    );

    reg.register_component(
        interner.intern("LED"),
        Interface({
            {interner.intern("v_in"), Domain::Electrical, Direction::Input},
            {interner.intern("ground"), Domain::Electrical, Direction::InOut},
        }),
        "Indicator light"
    );

    return reg;
}

} // namespace bp2
