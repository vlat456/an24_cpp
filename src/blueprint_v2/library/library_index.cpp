#include "library_index.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <spdlog/spdlog.h>
#include <stdexcept>

using json = nlohmann::json;

namespace bp2 {

LibraryIndex load_library_index(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open library index file: " + path);
    }

    std::string const content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    json j;
    try {
        j = json::parse(content);
    } catch (const json::parse_error& e) {
        throw std::runtime_error("Library index JSON parse error in '" + path + "': " + e.what());
    }

    if (!j.is_object()) {
        throw std::runtime_error("Library index must be a JSON object");
    }

    // Validate top-level fields: only "format", "version", "entries" are allowed
    static const std::set<std::string> allowed_top_keys = {"format", "version", "entries"};
    for (auto& [key, _] : j.items()) {
        if (allowed_top_keys.find(key) == allowed_top_keys.end()) {
            throw std::runtime_error("Library index: unknown top-level field '" + key + "'");
        }
    }

    // format
    if (!j.contains("format") || !j["format"].is_string()) {
        throw std::runtime_error("Library index: missing or non-string 'format'");
    }
    if (j["format"].get<std::string>() != "library_index") {
        throw std::runtime_error("Library index: expected format 'library_index', got '"
                                 + j["format"].get<std::string>() + "'");
    }

    // version
    if (!j.contains("version") || !j["version"].is_number_integer()) {
        throw std::runtime_error("Library index: missing or non-integer 'version'");
    }
    if (j["version"].get<int>() != 1) {
        throw std::runtime_error("Library index: expected version 1, got "
                                 + std::to_string(j["version"].get<int>()));
    }

    // entries
    if (!j.contains("entries") || !j["entries"].is_array()) {
        throw std::runtime_error("Library index: missing or non-array 'entries'");
    }

    LibraryIndex index;
    std::set<std::string> seen_ids;
    std::set<std::string> seen_paths;

    static const std::set<std::string> allowed_entry_keys = {"blueprint_id", "path"};

    for (size_t i = 0; i < j["entries"].size(); ++i) {
        const auto& entry = j["entries"][i];
        if (!entry.is_object()) {
            throw std::runtime_error("Library index: entries[" + std::to_string(i) + "] must be an object");
        }

        // Unknown fields check
        for (auto& [key, _] : entry.items()) {
            if (allowed_entry_keys.find(key) == allowed_entry_keys.end()) {
                throw std::runtime_error("Library index: entries[" + std::to_string(i)
                                         + "]: unknown field '" + key + "'");
            }
        }

        // blueprint_id
        if (!entry.contains("blueprint_id") || !entry["blueprint_id"].is_string()) {
            throw std::runtime_error("Library index: entries[" + std::to_string(i)
                                     + "]: missing or non-string 'blueprint_id'");
        }
        std::string const id = entry["blueprint_id"].get<std::string>();
        if (id.empty()) {
            throw std::runtime_error("Library index: entries[" + std::to_string(i)
                                     + "]: 'blueprint_id' must not be empty");
        }

        // path
        if (!entry.contains("path") || !entry["path"].is_string()) {
            throw std::runtime_error("Library index: entries[" + std::to_string(i)
                                     + "]: missing or non-string 'path'");
        }
        std::string const entry_path = entry["path"].get<std::string>();
        if (entry_path.empty()) {
            throw std::runtime_error("Library index: entries[" + std::to_string(i)
                                     + "]: 'path' must not be empty");
        }

        // Duplicate checks
        if (!seen_ids.insert(id).second) {
            throw std::runtime_error("Library index: duplicate blueprint_id '" + id + "'");
        }
        if (!seen_paths.insert(entry_path).second) {
            throw std::runtime_error("Library index: duplicate path '" + entry_path + "'");
        }

        index.entries[id] = entry_path;
    }

    spdlog::info("[library_index] Loaded {} entries from '{}'", index.entries.size(), path);
    return index;
}

} // namespace bp2
