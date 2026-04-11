#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace bp2 {

/// Canonical library index: maps blueprint_id → relative file path.
/// This is the sole path authority for referenced blueprint resolution.
/// Loaded from a dedicated library_index.json file (format "library_index", version 1).
struct LibraryIndex {
    std::unordered_map<std::string, std::string> entries;  // blueprint_id → path

    /// Resolve a blueprint_id to its file path.
    /// Returns std::nullopt if the blueprint_id is not in the index.
    std::optional<std::string> resolve(const std::string& blueprint_id) const {
        auto it = entries.find(blueprint_id);
        if (it != entries.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /// Check if a blueprint_id is present in the index.
    bool has(const std::string& blueprint_id) const {
        return entries.count(blueprint_id) > 0;
    }

    /// Number of entries in the index.
    size_t size() const { return entries.size(); }
};

/// Load a LibraryIndex from a JSON file.
/// Strict validation: format must be "library_index", version must be 1,
/// no unknown fields, no duplicate blueprint_id, no duplicate path.
/// Throws std::runtime_error on any validation failure.
LibraryIndex load_library_index(const std::string& path);

} // namespace bp2
