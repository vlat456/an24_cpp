/// Canonical embedded-path walking utilities.
/// InternedId variants are the authoritative implementations.
/// String-based overloads intern and delegate to the InternedId versions.

#include "embedded_path_utils.h"

#include "blueprint_v2/editor_model/editor_model.h"

#include <algorithm>
#include <functional>
#include <optional>

namespace editor {

// ============================================================================
// Canonical InternedId implementations
// ============================================================================

const bp2::Blueprint* resolve_embedded_blueprint_by_id(
    const bp2::Blueprint& root_bp,
    std::span<const ui::InternedId> path)
{
    const bp2::Blueprint* current = &root_bp;
    for (ui::InternedId segment : path) {
        const bp2::Blueprint::Node* node = current->find_node(segment);
        if (!node || !node->has_embedded_blueprint() || !node->blueprint_instance().source.inline_def()) {
            return nullptr;
        }
        current = node->blueprint_instance().source.inline_def();
    }
    return current;
}

ResolvedEmbeddedNode resolve_embedded_node_by_id(
    const bp2::Blueprint& root_bp,
    std::span<const ui::InternedId> path)
{
    if (path.empty()) {
        return {nullptr, nullptr};
    }

    const bp2::Blueprint* parent = &root_bp;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const bp2::Blueprint::Node* node = parent->find_node(path[i]);
        if (!node || !node->has_embedded_blueprint() || !node->blueprint_instance().source.inline_def()) {
            return {nullptr, nullptr};
        }
        parent = node->blueprint_instance().source.inline_def();
    }

    const bp2::Blueprint::Node* target = parent->find_node(path.back());
    if (!target) {
        return {nullptr, nullptr};
    }
    return {parent, target};
}

bool embedded_path_exists_by_id(
    const bp2::Blueprint& root_bp,
    std::span<const ui::InternedId> path)
{
    return resolve_embedded_blueprint_by_id(root_bp, path) != nullptr;
}

EmbeddedMutationResult mutate_embedded_blueprint_by_id(
    bp2::Blueprint root_bp,
    std::span<const ui::InternedId> path,
    const std::function<bp2::Blueprint(const bp2::Blueprint&)>& mutation)
{
    if (path.empty()) return {EmbeddedMutationResultKind::PathNotFound, std::nullopt};

    // Collect ancestor chain from root to the deepest parent (path[0..n-2]).
    struct Step {
        const bp2::Blueprint* bp;
        const bp2::Blueprint::Node* node;
    };
    std::vector<Step> chain;
    chain.reserve(path.size());

    const bp2::Blueprint* current = &root_bp;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const bp2::Blueprint::Node* node = current->find_node(path[i]);
        if (!node || !node->has_embedded_blueprint() || !node->blueprint_instance().source.inline_def()) {
            return {EmbeddedMutationResultKind::PathNotFound, std::nullopt};
        }
        chain.push_back({current, node});
        current = node->blueprint_instance().source.inline_def();
    }

    // Resolve the host node (last segment) within its parent blueprint.
    const bp2::Blueprint::Node* host = current->find_node(path.back());
    if (!host || !host->has_embedded_blueprint() || !host->blueprint_instance().source.inline_def()) {
        return {EmbeddedMutationResultKind::PathNotFound, std::nullopt};
    }

    // Apply the mutation to the innermost embedded blueprint.
    bp2::Blueprint mutated_inner = mutation(*host->blueprint_instance().source.inline_def());
    if (mutated_inner == *host->blueprint_instance().source.inline_def()) {
        return {EmbeddedMutationResultKind::NoChange, std::nullopt};
    }

    // Propagate back up: update the host node, then each ancestor in reverse.
    bp2::Blueprint::Node updated_host = *host;
    updated_host.blueprint_instance().source.set_inline_def(
        std::make_unique<bp2::Blueprint>(std::move(mutated_inner)));

    bp2::Blueprint result = bp2::replace_node_preserve_order(*current, std::move(updated_host));

    for (int i = static_cast<int>(chain.size()) - 1; i >= 0; --i) {
        bp2::Blueprint::Node updated_node = *chain[i].node;
        updated_node.blueprint_instance().source.set_inline_def(
            std::make_unique<bp2::Blueprint>(std::move(result)));
        result = bp2::replace_node_preserve_order(*chain[i].bp, std::move(updated_node));
    }

    return {EmbeddedMutationResultKind::Changed, std::move(result)};
}

// ============================================================================
// String-based convenience overloads — intern and delegate.
// ============================================================================

const bp2::Blueprint* resolve_embedded_blueprint(
    const bp2::Blueprint& root_bp,
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path)
{
    std::vector<ui::InternedId> iid_path;
    iid_path.reserve(scope_path.size());
    for (const std::string& segment : scope_path) {
        const ui::InternedId iid = interner.lookup(segment);
        if (iid.empty()) return nullptr;
        iid_path.push_back(iid);
    }
    return resolve_embedded_blueprint_by_id(root_bp, iid_path);
}

ResolvedEmbeddedNode resolve_embedded_node(
    const bp2::Blueprint& root_bp,
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path)
{
    std::vector<ui::InternedId> iid_path;
    iid_path.reserve(scope_path.size());
    for (const std::string& segment : scope_path) {
        const ui::InternedId iid = interner.lookup(segment);
        if (iid.empty()) return {nullptr, nullptr};
        iid_path.push_back(iid);
    }
    return resolve_embedded_node_by_id(root_bp, iid_path);
}

bool embedded_path_exists(
    const bp2::Blueprint& root_bp,
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path)
{
    return resolve_embedded_blueprint(root_bp, interner, scope_path) != nullptr;
}

EmbeddedMutationResult mutate_embedded_blueprint(
    bp2::Blueprint root_bp,
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path,
    const std::function<bp2::Blueprint(const bp2::Blueprint&)>& mutation)
{
    std::vector<ui::InternedId> iid_path;
    iid_path.reserve(scope_path.size());
    for (const std::string& segment : scope_path) {
        const ui::InternedId iid = interner.lookup(segment);
        if (iid.empty()) return {EmbeddedMutationResultKind::PathNotFound, std::nullopt};
        iid_path.push_back(iid);
    }
    return mutate_embedded_blueprint_by_id(std::move(root_bp), iid_path, mutation);
}

} // namespace editor
