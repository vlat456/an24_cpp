#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

/// Unified editor settings - handles recent files and open tabs.
/// Stored as JSON for extensibility.
class EditorSettings {
public:
    static constexpr size_t MAX_RECENT = 10;

    void addRecentFile(const std::string& path) {
        auto it = std::find(recent_files_.begin(), recent_files_.end(), path);
        if (it != recent_files_.end()) {
            recent_files_.erase(it);
        }
        recent_files_.insert(recent_files_.begin(), path);
        while (recent_files_.size() > MAX_RECENT) {
            recent_files_.pop_back();
        }
    }

    void addOpenTab(const std::string& path) {
        auto it = std::find(open_tabs_.begin(), open_tabs_.end(), path);
        if (it != open_tabs_.end()) {
            return;  // Already tracked — keep existing position
        }
        open_tabs_.push_back(path);  // Append to preserve left-to-right order
    }

    void removeRecentFile(const std::string& path) {
        auto it = std::find(recent_files_.begin(), recent_files_.end(), path);
        if (it != recent_files_.end()) {
            recent_files_.erase(it);
        }
    }

    void removeOpenTab(const std::string& path) {
        auto it = std::find(open_tabs_.begin(), open_tabs_.end(), path);
        if (it != open_tabs_.end()) {
            open_tabs_.erase(it);
        }
    }

    void clearRecentFiles() { recent_files_.clear(); }
    void clearOpenTabs() { open_tabs_.clear(); }

    const std::vector<std::string>& recentFiles() const { return recent_files_; }
    const std::vector<std::string>& openTabs() const { return open_tabs_; }
    bool hasOpenTabs() const { return !open_tabs_.empty(); }

    /// Load from JSON file, filtering out non-existent paths.
    void loadFrom(const std::string& filepath);

    /// Save to JSON file
    void saveTo(const std::string& filepath) const;

    /// Set the active tab (for restoring focus on startup)
    void setActiveTab(const std::string& path) { active_tab_ = path; }
    const std::string& activeTab() const { return active_tab_; }

private:
    std::vector<std::string> recent_files_;
    std::vector<std::string> open_tabs_;
    std::string active_tab_;
};
