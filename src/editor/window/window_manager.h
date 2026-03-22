#pragma once

#include "window/blueprint_window.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>

class WindowManager {
public:
    explicit WindowManager(bp2::EditorModel& model, ui::StringInterner& interner,
                           bp2::PathArena& arena)
        : model_(model), interner_(interner), arena_(arena)
    {
        windows_.push_back(std::make_unique<BlueprintWindow>(
            model_, interner_, arena_, "", "Root"));
    }

    BlueprintWindow& root() { return *windows_[0]; }
    const std::vector<std::unique_ptr<BlueprintWindow>>& windows() const { return windows_; }
    std::vector<std::unique_ptr<BlueprintWindow>>& windows() { return windows_; }

    BlueprintWindow* open(const std::string& group_id, const std::string& title) {
        for (auto& w : windows_) {
            if (w->group_id == group_id) { w->open = true; return w.get(); }
        }
        windows_.push_back(std::make_unique<BlueprintWindow>(
            model_, interner_, arena_, group_id, title));
        return windows_.back().get();
    }

    void close(const std::string& group_id) {
        if (group_id.empty()) return;
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                [&](const std::unique_ptr<BlueprintWindow>& w) {
                    return w->group_id == group_id;
                }),
            windows_.end());
    }

    void remove_closed_windows() {
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                [](const std::unique_ptr<BlueprintWindow>& w) {
                    return !w->open && !w->group_id.empty();
                }),
            windows_.end());
    }

    void remove_orphaned_windows() {
        std::unordered_set<std::string> live;
        for (auto const& n : model_.current().nested())
            live.insert(std::string(interner_.resolve(n.id)));
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                [&](const std::unique_ptr<BlueprintWindow>& w) {
                    return !w->group_id.empty() && !live.count(w->group_id);
                }),
            windows_.end());
    }

    BlueprintWindow* find(const std::string& group_id) {
        for (auto& w : windows_)
            if (w->group_id == group_id) return w.get();
        return nullptr;
    }

    void close_all() {
        if (windows_.size() > 1)
            windows_.erase(windows_.begin() + 1, windows_.end());
    }

    size_t count() const { return windows_.size(); }

private:
    bp2::EditorModel& model_;
    ui::StringInterner& interner_;
    bp2::PathArena& arena_;
    std::vector<std::unique_ptr<BlueprintWindow>> windows_;
};
