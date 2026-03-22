#include "bake_ops.h"

#include <stdexcept>
#include <unordered_set>

namespace bp2 {

namespace {

Blueprint bake_all_impl(Blueprint const& bp,
                        TypeRegistry const& registry,
                        std::unordered_set<ui::InternedId>& active_refs) {
    Blueprint result = bp;

    for (auto const& nested : bp.nested()) {
        bool inserted_ref = false;
        ui::InternedId ref_id;

        if (!nested.embedded) {
            ref_id = nested.blueprint_id;
            if (active_refs.count(ref_id) > 0) {
                throw std::runtime_error("bake_all: cyclic blueprint reference detected");
            }
            active_refs.insert(ref_id);
            inserted_ref = true;

            result = bake_nested(result, nested.id, registry);
        }

        auto const* current_nested = result.find_nested(nested.id);
        if (current_nested && current_nested->embedded && current_nested->inline_def) {
            Blueprint baked_child = bake_all_impl(*current_nested->inline_def, registry, active_refs);

            Blueprint::Nested updated;
            updated.id = current_nested->id;
            updated.blueprint_id = current_nested->blueprint_id;
            updated.embedded = true;
            updated.inline_def = std::make_unique<Blueprint>(std::move(baked_child));
            updated.iface = current_nested->iface;
            updated.x = current_nested->x;
            updated.y = current_nested->y;

            result = result.without_nested(current_nested->id).with_nested(std::move(updated));
        }

        if (inserted_ref) {
            active_refs.erase(ref_id);
        }
    }

    return result;
}

} // namespace

Blueprint bake_nested(Blueprint const& bp,
                      ui::InternedId nested_id,
                      TypeRegistry const& registry) {
    auto const* nested = bp.find_nested(nested_id);
    if (!nested) {
        throw std::runtime_error("Nested not found");
    }
    if (nested->embedded) {
        throw std::runtime_error("Already embedded");
    }

    auto const* entry = registry.find(nested->blueprint_id);
    if (!entry || !entry->blueprint) {
        throw std::runtime_error("Unknown blueprint reference");
    }

    Blueprint::Nested baked;
    baked.id = nested->id;
    baked.blueprint_id = {};
    baked.embedded = true;
    baked.inline_def = std::make_unique<Blueprint>(entry->blueprint->clone(nested->id));
    baked.iface = nested->iface;
    baked.x = nested->x;
    baked.y = nested->y;

    return bp.without_nested(nested_id).with_nested(std::move(baked));
}

std::optional<UnbakeResult> try_unbake(Blueprint const& bp,
                                       ui::InternedId nested_id,
                                       TypeRegistry const& registry) {
    auto const* nested = bp.find_nested(nested_id);
    if (!nested || !nested->embedded || !nested->inline_def) {
        return std::nullopt;
    }

    for (auto const& kv : registry) {
        auto const& id = kv.first;
        auto const& entry = kv.second;
        if (!entry.is_blueprint || !entry.blueprint) {
            continue;
        }

        if (*entry.blueprint == *nested->inline_def) {
            Blueprint::Nested ref;
            ref.id = nested->id;
            ref.blueprint_id = id;
            ref.embedded = false;
            ref.iface = nested->iface;
            ref.x = nested->x;
            ref.y = nested->y;

            UnbakeResult out;
            out.blueprint = bp.without_nested(nested_id).with_nested(std::move(ref));
            out.referenced_id = id;
            return out;
        }
    }

    return std::nullopt;
}

Blueprint bake_all(Blueprint const& bp,
                   TypeRegistry const& registry) {
    std::unordered_set<ui::InternedId> active_refs;
    return bake_all_impl(bp, registry, active_refs);
}

} // namespace bp2
