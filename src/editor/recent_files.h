#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

/// Recent files list - pure business logic.
/// Persistence is handled via loadFrom()/saveTo() with file I/O.
class RecentFiles {
public:
    static constexpr size_t MAX = 10;

    void add(const std::string& path) {
        auto it = std::find(files_.begin(), files_.end(), path);
        if (it != files_.end()) {
            files_.erase(it);
        }
        files_.insert(files_.begin(), path);
        while (files_.size() > MAX) {
            files_.pop_back();
        }
    }

    void remove(const std::string& path) {
        auto it = std::find(files_.begin(), files_.end(), path);
        if (it != files_.end()) {
            files_.erase(it);
        }
    }

    void clear() { files_.clear(); }
    const std::vector<std::string>& files() const { return files_; }
    bool empty() const { return files_.empty(); }

    /// Load from file, filtering out non-existent paths
    void loadFrom(const std::string& filepath);

    /// Save to file
    void saveTo(const std::string& filepath) const;

private:
    std::vector<std::string> files_;
};
