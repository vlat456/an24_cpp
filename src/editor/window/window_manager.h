#pragma once

#include "window/blueprint_window.h"
#include <memory>
#include <string>
#include <vector>

/// Manages multiple editor windows (one per nesting level).
/// Owns all BlueprintWindows; the root window is always present.
class WindowManager {
public:
    /// Construct with a shared blueprint. Creates the root window.
    explicit WindowManager(Blueprint& bp)
        : bp_(bp)
    {
        windows_.push_back(std::make_unique<BlueprintWindow>(bp, "", "Root"));
    }

    /// The root window (always index 0, always open).
    BlueprintWindow& root() { return *windows_[0]; }

    /// All windows (for rendering iteration).
    const std::vector<std::unique_ptr<BlueprintWindow>>& windows() const { return windows_; }
    std::vector<std::unique_ptr<BlueprintWindow>>& windows() { return windows_; }

    /// Open a sub-window for a collapsed group. Returns the window.
    /// If already open, returns the existing window.
    BlueprintWindow* open(const std::string& group_id, const std::string& title) {
        // Check if already open
        for (auto& w : windows_) {
            if (w->group_id == group_id) {
                w->open = true;
                return w.get();
            }
        }
        windows_.push_back(std::make_unique<BlueprintWindow>(bp_, group_id, title));
        return windows_.back().get();
    }

    /// Close a sub-window by group_id. Root window cannot be closed.
    void close(const std::string& group_id) {
        if (group_id.empty()) return;  // never close root
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                [&group_id](const std::unique_ptr<BlueprintWindow>& w) {
                    return w->group_id == group_id;
                }),
            windows_.end());
    }

    /// Remove windows that the user closed (open == false), except root.
    void removeClosedWindows() {
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                [](const std::unique_ptr<BlueprintWindow>& w) {
                    return !w->open && !w->group_id.empty();
                }),
            windows_.end());
    }

    /// Find window by group_id (nullptr if not found).
    BlueprintWindow* find(const std::string& group_id) {
        for (auto& w : windows_) {
            if (w->group_id == group_id) return w.get();
        }
        return nullptr;
    }

    /// Close all sub-windows (keep root only).
    void closeAll() {
        if (windows_.size() > 1)
            windows_.erase(windows_.begin() + 1, windows_.end());
    }

    /// Number of open windows.
    size_t count() const { return windows_.size(); }

private:
    Blueprint& bp_;
    std::vector<std::unique_ptr<BlueprintWindow>> windows_;
};
