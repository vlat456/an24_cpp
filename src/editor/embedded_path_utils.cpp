/// InternedId-native embedded-path utilities.
/// Only `intern_scope_path` remains as a non-trivial implementation
/// (serialization boundary: string → InternedId conversion).
/// All other functions are inline delegations in the header.

#include "embedded_path_utils.h"

#include <optional>
#include <vector>

namespace editor {

std::optional<std::vector<core::InternedId>> intern_scope_path(
    const core::StringInterner& interner,
    std::span<const std::string> scope_path)
{
    std::vector<core::InternedId> iid_path;
    iid_path.reserve(scope_path.size());
    for (const std::string& segment : scope_path) {
        const core::InternedId iid = interner.lookup(segment);
        if (iid.empty()) return std::nullopt;
        iid_path.push_back(iid);
    }
    return iid_path;
}

} // namespace editor
