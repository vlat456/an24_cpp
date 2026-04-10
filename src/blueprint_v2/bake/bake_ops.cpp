#include "bake_ops.h"

#include <stdexcept>
#include <unordered_set>

namespace bp2 {

namespace {

Blueprint bake_all_impl(Blueprint const& bp,
                        BlueprintLibrary const& library,
                        std::unordered_set<ui::InternedId>& active_refs);

Blueprint bake_node_blueprint_instance(Blueprint const& bp,
                                      ui::InternedId node_id,
                                      BlueprintLibrary const& library) {
    auto const* node = bp.find_blueprint_instance(node_id);
    if (!node) {
        throw std::runtime_error("Blueprint instance node not found");
    }
    if (!node->source.has_value()) {
        throw std::runtime_error("Blueprint instance has no source");
    }
    if (node->source->is_embedded()) {
        throw std::runtime_error("Already embedded");
    }

    auto const* referenced = library.find(node->source->blueprint_id());
    if (!referenced) {
        throw std::runtime_error("Unknown blueprint reference");
    }

    auto updated_node = *node;
    updated_node.source = Blueprint::Node::BlueprintSource::make_embedded(
        node->source->blueprint_id(),
        std::make_unique<Blueprint>(referenced->clone(node_id)));

    return bp.without_node(node_id).with_node(std::move(updated_node));
}

Blueprint bake_all_impl(Blueprint const& bp,
                        BlueprintLibrary const& library,
                        std::unordered_set<ui::InternedId>& active_refs) {
    Blueprint result = bp;

    for (auto const& node : bp.nodes()) {
        if (!node.is_blueprint_instance() || !node.source.has_value()) {
            continue;
        }

        bool inserted_ref = false;
        ui::InternedId ref_id;

        if (node.source->is_reference()) {
            ref_id = node.source->blueprint_id();
            if (active_refs.count(ref_id) > 0) {
                throw std::runtime_error("bake_all: cyclic blueprint reference detected");
            }
            active_refs.insert(ref_id);
            inserted_ref = true;

            result = bake_node_blueprint_instance(result, node.semantic.id, library);
        }

        auto const* current_node = result.find_blueprint_instance(node.semantic.id);
        if (current_node && current_node->source.has_value() && current_node->source->is_embedded()) {
            Blueprint baked_child = bake_all_impl(*current_node->source->inline_def(), library, active_refs);

            auto updated_node = *current_node;
            updated_node.source->set_inline_def(std::make_unique<Blueprint>(std::move(baked_child)));
            result = result.without_node(current_node->semantic.id).with_node(std::move(updated_node));
        }

        if (inserted_ref) {
            active_refs.erase(ref_id);
        }
    }

    return result;
}

} // namespace

std::optional<UnbakeResult> try_unbake(Blueprint const& bp,
                                       ui::InternedId node_id,
                                       BlueprintLibrary const& library) {
    auto const* node = bp.find_blueprint_instance(node_id);
    if (!node || !node->source.has_value() || !node->source->is_embedded()) {
        return std::nullopt;
    }

    for (auto const& kv : library) {
        auto const& id = kv.first;
        auto const& referenced = kv.second;
        if (referenced == *node->source->inline_def()) {
            auto updated_node = *node;
            updated_node.source = Blueprint::Node::BlueprintSource::make_reference(
                id,
                node->source->resolved_iface());

            UnbakeResult out;
            out.blueprint = bp.without_node(node_id).with_node(std::move(updated_node));
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
