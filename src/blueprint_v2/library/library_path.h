#pragma once

#include <optional>
#include <string>

namespace bp2 {

struct LibraryIndex;

/// Resolve a blueprint_id to its library file path using the LibraryIndex.
/// This is the canonical path resolution — the LibraryIndex is the sole path authority.
std::optional<std::string> resolve_library_blueprint_path(const LibraryIndex& index,
                                                          const std::string& blueprint_id);

} // namespace bp2
