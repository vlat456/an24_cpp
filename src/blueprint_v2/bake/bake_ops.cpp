#include "bake_ops.h"

#include <stdexcept>
#include <unordered_set>

namespace bp2 {

namespace {

Blueprint bake_all_impl(Blueprint const& bp,
                        BlueprintLibrary const& library,
                        std::unordered_set<ui::InternedId>& active_refs) {
    Blueprint result = bp;

    for (auto const& nested : bp.nested()) {
        bool inserted_ref = false;
        ui::InternedId ref_id;

        if (nested.is_reference()) {
            ref_id = nested.blueprint_id();
            if (active_refs.count(ref_id) > 0) {
                throw std::runtime_error("bake_all: cyclic blueprint reference detected");
            }
            active_refs.insert(ref_id);
            inserted_ref = true;

            result = bake_nested(result, nested.id, library);
        }

        auto const* current_nested = result.find_nested(nested.id);
        if (current_nested && current_nested->is_embedded()) {
            Blueprint baked_child = bake_all_impl(*current_nested->inline_def(), library, active_refs);

            auto updated = Blueprint::Nested::make_embedded(
                current_nested->id,
                current_nested->blueprint_id(),
                std::make_unique<Blueprint>(std::move(baked_child)),
                current_nested->x, current_nested->y);

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
                      BlueprintLibrary const& library) {
    auto const* nested = bp.find_nested(nested_id);
    if (!nested) {
        throw std::runtime_error("Nested not found");
    }
    if (nested->is_embedded()) {
        throw std::runtime_error("Already embedded");
    }

    auto const* referenced = library.find(nested->blueprint_id());
    if (!referenced) {
        throw std::runtime_error("Unknown blueprint reference");
    }

    auto baked = Blueprint::Nested::make_embedded(
        nested->id,
        {},
        std::make_unique<Blueprint>(referenced->clone(nested->id)),
        nested->x, nested->y);

    return bp.without_nested(nested_id).with_nested(std::move(baked));
}

std::optional<UnbakeResult> try_unbake(Blueprint const& bp,
                                       ui::InternedId nested_id,
                                       BlueprintLibrary const& library) {
    auto const* nested = bp.find_nested(nested_id);
    if (!nested || !nested->is_embedded()) {
        return std::nullopt;
    }

    for (auto const& kv : library) {
        auto const& id = kv.first;
        auto const& referenced = kv.second;
        if (referenced == *nested->inline_def()) {
            auto ref = Blueprint::Nested::make_reference(
                nested->id, id, nested->resolved_iface(),
                nested->x, nested->y);

            UnbakeResult out;
            out.blueprint = bp.without_nested(nested_id).with_nested(std::move(ref));
            out.referenced_id = id;
            return out;
        }
    }

    return std::nullopt;
}

Blueprint bake_all(Blueprint const& bp,
                   BlueprintLibrary const& library) {
    std::unordered_set<ui::InternedId> active_refs;
    return bake_all_impl(bp, library, active_refs);
}

} // namespace bp2
