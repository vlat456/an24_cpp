#pragma once

#include "window/blueprint_window.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <unordered_set>

struct TypeRegistry;

class WindowManager {
public:
    explicit WindowManager(bp2::EditorModel& model, ui::StringInterner& interner,
                           bp2::PathArena& arena,
                           const TypeRegistry* parser_registry = nullptr)
        : model_(model), interner_(interner), arena_(arena), parser_registry_(parser_registry)
    {
        windows_.push_back(std::make_unique<BlueprintWindow>(
            model_, interner_, arena_, "", "Root", parser_registry_));
    }

    void set_parser_registry(const TypeRegistry* parser_registry) {
        parser_registry_ = parser_registry;
        for (auto& w : windows_) {
            w->input.set_parser_registry(parser_registry_);
        }
    }

    BlueprintWindow& root() { return *windows_[0]; }
    const BlueprintWindow& root() const { return *windows_[0]; }
    const std::vector<std::unique_ptr<BlueprintWindow>>& windows() const { return windows_; }
    std::vector<std::unique_ptr<BlueprintWindow>>& windows() { return windows_; }

    std::pair<BlueprintWindow*, bool> open(const std::string& group_id, const std::string& title) {
        for (auto& w : windows_) {
            if (w->group_id == group_id) {
                w->open = true;
                return {w.get(), false};
            }
        }
        windows_.push_back(std::make_unique<BlueprintWindow>(
            model_, interner_, arena_, group_id, title, parser_registry_));
        return {windows_.back().get(), true};
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
                    // External-ref windows are kept alive (not tied to nested groups)
                    if (w->mode == BlueprintWindowMode::ExternalReference) return false;
                    return !w->group_id.empty() && !live.count(w->group_id);
                }),
            windows_.end());
    }

    BlueprintWindow* find(const std::string& group_id) {
        for (auto& w : windows_)
            if (w->group_id == group_id) return w.get();
        return nullptr;
    }

    /// Find an external-reference window by parent_instance_id.
    BlueprintWindow* find_external(const std::string& parent_instance_id) {
        for (auto& w : windows_) {
            if (w->mode == BlueprintWindowMode::ExternalReference
                && w->parent_instance_id == parent_instance_id) {
                return w.get();
            }
        }
        return nullptr;
    }

    /// Open an external reference sub-window.
    /// The caller must fill in external_blueprint, external_interner, external_arena,
    /// and parent_instance_id on the returned window, then call rebuild on the scene.
    BlueprintWindow* open_external_stub(const std::string& parent_instance_id,
                                        const std::string& title) {
        // Reuse existing window for the same parent instance
        if (auto* existing = find_external(parent_instance_id)) {
            existing->open = true;
            return existing;
        }
        // Use a synthetic group_id that won't collide with real nested group IDs.
        // The "extref:" prefix ensures no collision with regular group_id values.
        std::string synthetic_group_id = "extref:" + parent_instance_id;
        windows_.push_back(std::make_unique<BlueprintWindow>(
            model_, interner_, arena_, synthetic_group_id, title, parser_registry_));
        auto* win = windows_.back().get();
        win->mode = BlueprintWindowMode::ExternalReference;
        win->parent_instance_id = parent_instance_id;
        return win;
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
    const TypeRegistry* parser_registry_ = nullptr;
    std::vector<std::unique_ptr<BlueprintWindow>> windows_;
};
