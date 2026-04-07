#pragma once

#include "blueprint_v2/blueprint/blueprint.h"

#include <unordered_map>

namespace bp2 {

class BlueprintLibrary {
public:
    void add(ui::InternedId id, Blueprint blueprint);
    const Blueprint* find(ui::InternedId id) const;

    auto begin() const { return entries_.begin(); }
    auto end() const { return entries_.end(); }

private:
    std::unordered_map<ui::InternedId, Blueprint> entries_;
};

} // namespace bp2
