/// String-based convenience overloads for embedded-path operations.
/// Interns string paths and delegates to the canonical bp2:: implementations.

#include "embedded_path_utils.h"

#include <algorithm>
#include <functional>
#include <optional>

namespace editor {

// ============================================================================
// Path conversion helper
// ============================================================================

std::optional<std::vector<ui::InternedId>> intern_scope_path(
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path)
{
    std::vector<ui::InternedId> iid_path;
    iid_path.reserve(scope_path.size());
    for (const std::string& segment : scope_path) {
        const ui::InternedId iid = interner.lookup(segment);
        if (iid.empty()) return std::nullopt;
        iid_path.push_back(iid);
    }
    return iid_path;
}

// ============================================================================
// String-based convenience overloads — intern and delegate.
// ============================================================================

const bp2::Blueprint* resolve_embedded_blueprint(
    const bp2::Blueprint& root_bp,
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path)
{
    auto iid_path = intern_scope_path(interner, scope_path);
    if (!iid_path) return nullptr;
    return bp2::resolve_embedded_blueprint(root_bp, *iid_path);
}

ResolvedEmbeddedNode resolve_embedded_node(
    const bp2::Blueprint& root_bp,
    const ui::StringInterner& interner,
    std::span<const std::string> scope_path)
{
    auto iid_path = intern_scope_path(interner, scope_path);
    if (!iid_path) return {nullptr, nullptr};
    return bp2::resolve_embedded_node(root_bp, *iid_path);
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
    auto iid_path = intern_scope_path(interner, scope_path);
    if (!iid_path) return {EmbeddedMutationResultKind::PathNotFound, std::nullopt};
    return bp2::mutate_embedded_blueprint(std::move(root_bp), *iid_path, mutation);
}

} // namespace editor
