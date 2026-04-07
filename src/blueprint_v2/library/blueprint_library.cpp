#include "blueprint_library.h"

namespace bp2 {

void BlueprintLibrary::add(ui::InternedId id, Blueprint blueprint) {
    entries_[id] = std::move(blueprint);
}

const Blueprint* BlueprintLibrary::find(ui::InternedId id) const {
    auto it = entries_.find(id);
    if (it == entries_.end()) {
        return nullptr;
    }
    return &it->second;
}

} // namespace bp2
