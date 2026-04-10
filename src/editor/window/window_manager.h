#pragma once

#include "window/blueprint_window.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unordered_set>
#include <spdlog/spdlog.h>

struct TypeRegistry;

class WindowManager {
public:
    explicit WindowManager(bp2::EditorModel& model, ui::StringInterner& interner,
                           bp2::PathArena& arena,
                           const TypeRegistry* parser_registry = nullptr)
        : model_(model), interner_(interner), arena_(arena), parser_registry_(parser_registry)
    {
        windows_.push_back(std::make_unique<BlueprintWindow>(
            RootWindowTag{}, model_, interner_, arena_, "Root", parser_registry_));
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

    BlueprintWindow* find(const WindowScopeId& scope_id) {
        for (auto& w : windows_) {
            if (w->resolved_scope_id() == scope_id) {
                return w.get();
            }
        }
        return nullptr;
    }

    std::pair<BlueprintWindow*, bool> open(const WindowScopeId& scope_id, const std::string& title) {
        if (auto* existing = find(scope_id)) {
            existing->open = true;
            return {existing, false};
        }

        try {
            if (scope_id.is_root()) {
                throw std::logic_error("WindowManager::open cannot create an additional root window");
            }
            if (scope_id.is_embedded()) {
                windows_.push_back(std::make_unique<BlueprintWindow>(
                    EmbeddedWindowTag{}, model_, interner_, arena_, scope_id.key(), title, parser_registry_));
            } else if (scope_id.is_external()) {
                windows_.push_back(std::make_unique<BlueprintWindow>(
                    ExternalWindowTag{}, model_, interner_, arena_, scope_id.key(), title, parser_registry_));
            } else {
                throw std::logic_error("WindowManager::open received unknown WindowScopeId mode");
            }
        } catch (const std::logic_error& e) {
            spdlog::error("[editor] Failed to open window '{}' (scope '{}'): {}",
                          title, scope_id.key(), e.what());
            return {nullptr, false};
        }
        return {windows_.back().get(), true};
    }

    void close(const WindowScopeId& scope_id) {
        if (scope_id.is_root()) return;
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                [&](const std::unique_ptr<BlueprintWindow>& w) {
                    return w->resolved_scope_id() == scope_id;
                }),
            windows_.end());
    }

    void remove_closed_windows() {
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                [](const std::unique_ptr<BlueprintWindow>& w) {
                    return !w->open && !w->resolved_scope_id().is_root();
                }),
            windows_.end());
    }

    void remove_orphaned_windows() {
        std::unordered_set<std::string> live;
        // Collect all blueprint-instance node IDs
        for (const auto& node : model_.current().nodes()) {
            if (node.is_blueprint_instance()) {
                live.insert(std::string(interner_.resolve(node.semantic.id)));
            }
        }
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                [&](const std::unique_ptr<BlueprintWindow>& w) {
                    // External-ref windows are kept alive (not tied to blueprint instances)
                    if (w->resolved_scope_id().is_external()) return false;
                    const auto& typed_scope = w->resolved_scope_id();
                    return typed_scope.is_embedded() && !live.count(typed_scope.key());
                }),
            windows_.end());
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
