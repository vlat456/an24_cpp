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

    std::pair<BlueprintWindow*, bool> open(const std::string& scope_id, const std::string& title) {
        const auto typed_scope = WindowScopeId::embedded(scope_id);
        for (auto& w : windows_) {
            if (w->resolved_scope_id() == typed_scope) {
                w->open = true;
                return {w.get(), false};
            }
        }
        try {
            windows_.push_back(std::make_unique<BlueprintWindow>(
                EmbeddedWindowTag{}, model_, interner_, arena_, scope_id, title, parser_registry_));
        } catch (const std::logic_error& e) {
            spdlog::error("[editor] Failed to open window '{}' (group '{}'): {}",
                          title, scope_id, e.what());
            return {nullptr, false};
        }
        return {windows_.back().get(), true};
    }

    void close(const std::string& scope_id) {
        if (scope_id.empty()) return;
        const auto typed_scope = WindowScopeId::embedded(scope_id);
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                [&](const std::unique_ptr<BlueprintWindow>& w) {
                    return w->resolved_scope_id() == typed_scope;
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
        for (auto const& n : model_.current().nested())
            live.insert(std::string(interner_.resolve(n.id)));
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                [&](const std::unique_ptr<BlueprintWindow>& w) {
                    // External-ref windows are kept alive (not tied to nested groups)
                    if (w->mode == BlueprintWindowMode::ExternalReference) return false;
                    const auto& typed_scope = w->resolved_scope_id();
                    return typed_scope.is_embedded() && !live.count(typed_scope.key());
                }),
            windows_.end());
    }

    BlueprintWindow* find(const std::string& scope_id) {
        if (scope_id.empty()) return nullptr;
        const auto typed_scope = WindowScopeId::embedded(scope_id);
        for (auto& w : windows_)
            if (w->resolved_scope_id() == typed_scope) return w.get();
        return nullptr;
    }

    /// Find an external-reference window by parent_instance_id.
    BlueprintWindow* find_external(const std::string& parent_instance_id) {
        const auto typed_scope = WindowScopeId::external(parent_instance_id);
        for (auto& w : windows_) {
            if (w->resolved_scope_id() == typed_scope) {
                return w.get();
            }
        }
        return nullptr;
    }

    /// Open an external reference sub-window.
    /// The caller must fill in external_blueprint, external_interner, external_arena
    /// on the returned window, then call rebuild on the scene.
    BlueprintWindow* open_external_stub(const std::string& parent_instance_id,
                                        const std::string& title) {
        // Reuse existing window for the same parent instance
        if (auto* existing = find_external(parent_instance_id)) {
            existing->open = true;
            return existing;
        }
        // External-ref windows use typed WindowScopeId for identity.
        // scope_id remains empty to avoid collision with nested group identities.
        windows_.push_back(std::make_unique<BlueprintWindow>(
            RootWindowTag{}, model_, interner_, arena_, title, parser_registry_));
        auto* win = windows_.back().get();
        win->set_external_identity(parent_instance_id);
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
