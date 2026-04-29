#include "embedded_mutation.h"

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/blueprint/blueprint_replace.h"

#include <algorithm>
#include <functional>
#include <optional>

namespace bp2 {

// ============================================================================
// Path walking (read-only)
// ============================================================================

const Blueprint* resolve_embedded_blueprint(
    const Blueprint& root_bp,
    std::span<const core::InternedId> path)
{
    const Blueprint* current = &root_bp;
    for (core::InternedId segment : path) {
        const Blueprint::Node* node = current->find_node(segment);
        if (!node || !node->has_embedded_blueprint() || !node->blueprint_instance().source.inline_def()) {
            return nullptr;
        }
        current = node->blueprint_instance().source.inline_def();
    }
    return current;
}

ResolvedEmbeddedNode resolve_embedded_node(
    const Blueprint& root_bp,
    std::span<const core::InternedId> path)
{
    if (path.empty()) {
        return {nullptr, nullptr};
    }

    const Blueprint* parent = &root_bp;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const Blueprint::Node* node = parent->find_node(path[i]);
        if (!node || !node->has_embedded_blueprint() || !node->blueprint_instance().source.inline_def()) {
            return {nullptr, nullptr};
        }
        parent = node->blueprint_instance().source.inline_def();
    }

    const Blueprint::Node* target = parent->find_node(path.back());
    if (!target) {
        return {nullptr, nullptr};
    }
    return {parent, target};
}

const Blueprint::Node* find_embedded_node(
    const Blueprint& root_bp,
    std::span<const core::InternedId> path)
{
    if (path.empty()) return nullptr;

    const Blueprint* current = &root_bp;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const Blueprint::Node* node = current->find_node(path[i]);
        if (!node || !node->has_embedded_blueprint() || !node->blueprint_instance().source.inline_def()) {
            return nullptr;
        }
        current = node->blueprint_instance().source.inline_def();
    }
    return current->find_node(path.back());
}

bool embedded_path_exists(
    const Blueprint& root_bp,
    std::span<const core::InternedId> path)
{
    return resolve_embedded_blueprint(root_bp, path) != nullptr;
}

// ============================================================================
// Mutation (produces new root Blueprint)
// ============================================================================

EmbeddedMutationResult mutate_embedded_blueprint(
    Blueprint root_bp,
    std::span<const core::InternedId> path,
    const std::function<Blueprint(const Blueprint&)>& mutation)
{
    if (path.empty()) return {EmbeddedMutationResultKind::PathNotFound, std::nullopt};

    // Collect ancestor chain from root to the deepest parent (path[0..n-2]).
    struct Step {
        const Blueprint* bp;
        const Blueprint::Node* node;
    };
    std::vector<Step> chain;
    chain.reserve(path.size());

    const Blueprint* current = &root_bp;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const Blueprint::Node* node = current->find_node(path[i]);
        if (!node || !node->has_embedded_blueprint() || !node->blueprint_instance().source.inline_def()) {
            return {EmbeddedMutationResultKind::PathNotFound, std::nullopt};
        }
        chain.push_back({current, node});
        current = node->blueprint_instance().source.inline_def();
    }

    // Resolve the host node (last segment) within its parent blueprint.
    const Blueprint::Node* host = current->find_node(path.back());
    if (!host || !host->has_embedded_blueprint() || !host->blueprint_instance().source.inline_def()) {
        return {EmbeddedMutationResultKind::PathNotFound, std::nullopt};
    }

    // Apply the mutation to the innermost embedded blueprint.
    Blueprint mutated_inner = mutation(*host->blueprint_instance().source.inline_def());
    if (mutated_inner == *host->blueprint_instance().source.inline_def()) {
        return {EmbeddedMutationResultKind::NoChange, std::nullopt};
    }

    // Propagate back up: update the host node, then each ancestor in reverse.
    Blueprint::Node updated_host = *host;
    updated_host.blueprint_instance().source.set_inline_def(
        std::make_unique<Blueprint>(std::move(mutated_inner)));

    Blueprint result = replace_node_preserve_order(*current, std::move(updated_host));

    for (int i = static_cast<int>(chain.size()) - 1; i >= 0; --i) {
        Blueprint::Node updated_node = *chain[i].node;
        updated_node.blueprint_instance().source.set_inline_def(
            std::make_unique<Blueprint>(std::move(result)));
        result = replace_node_preserve_order(*chain[i].bp, std::move(updated_node));
    }

    return {EmbeddedMutationResultKind::Changed, std::move(result)};
}

} // namespace bp2
