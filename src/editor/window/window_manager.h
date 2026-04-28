#pragma once

#include "window/blueprint_window.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "document_simulation_internal.h"
#include "embedded_path_utils.h"
#include "core/strings/interned_id.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unordered_set>
#include <optional>
#include <spdlog/spdlog.h>

struct ComponentRegistry;

class WindowManager {
public:
    explicit WindowManager(bp2::EditorModel& model, core::StringInterner& interner,
                       bp2::PathArena& arena,
                       const ComponentRegistry* type_registry = nullptr,
                       const editor::IconFont* icon_font = nullptr)
        : model_(model), interner_(interner), arena_(arena), type_registry_(type_registry), icon_font_(icon_font)
    {
        windows_.push_back(BlueprintWindow::create_root(context(), "Root"));
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

    const BlueprintWindow* find(const WindowScopeId& scope_id) const {
        for (const auto& w : windows_) {
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
                windows_.push_back(BlueprintWindow::create_embedded(context(), scope_id, title));
            } else if (scope_id.is_external()) {
                throw std::logic_error("WindowManager::open_external must be used for external scopes");
            } else {
                throw std::logic_error("WindowManager::open received unknown WindowScopeId mode");
            }
        } catch (const std::logic_error& e) {
            spdlog::error("[editor] Failed to open window '{}' (scope '{}'): {}",
                          title, editor::instance_path_to_scope_string(interner_, scope_id.path()), e.what());
            return {nullptr, false};
        }
        return {windows_.back().get(), true};
    }

    std::pair<BlueprintWindow*, bool> open_external(const WindowScopeId& scope_id,
                                                    std::string title,
                                                    BlueprintWindow::ExternalDocument external_document) {
        if (auto* existing = find(scope_id)) {
            existing->open = true;
            return {existing, false};
        }

        try {
            if (!scope_id.is_external()) {
                throw std::logic_error("WindowManager::open_external requires external scope");
            }
            windows_.push_back(BlueprintWindow::create_external(
                context(), scope_id, std::move(title), std::move(external_document)));
        } catch (const std::logic_error& e) {
            spdlog::error("[editor] Failed to open external window '{}' (scope '{}'): {}",
                          title, editor::instance_path_to_scope_string(interner_, scope_id.path()), e.what());
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
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                [&](const std::unique_ptr<BlueprintWindow>& w) {
                    if (w->resolved_scope_id().is_root()) return false;

                    if (w->resolved_scope_id().is_embedded()) {
                        // Embedded windows are orphaned when their path no longer resolves.
                        return !editor::embedded_path_exists(
                            model_.current(), w->resolved_scope_id().path());
                    }

                    if (w->resolved_scope_id().is_external()) {
                        // External-ref windows are valid only while their owner
                        // path still resolves to a referenced blueprint instance
                        // with the same referenced blueprint id. If the owner is
                        // baked in or retargeted, the stale external window must
                        // self-close to avoid split-brain.
                        return !external_window_still_valid(*w);
                    }

                    return false;
                }),
            windows_.end());
    }

    void close_all() {
        if (windows_.size() > 1)
            windows_.erase(windows_.begin() + 1, windows_.end());
    }

    size_t count() const { return windows_.size(); }

private:
    bool external_window_still_valid(const BlueprintWindow& window) const {
        const WindowScopeId& scope_id = window.resolved_scope_id();
        if (!scope_id.is_external() || scope_id.path().empty()) {
            return false;
        }

        const auto [bp, bp_interner] = resolve_parent_blueprint_for_child_scope(scope_id.path());
        if (!bp || !bp_interner) {
            return false;
        }

        // scope_id.path().back() now returns InternedId - use directly to find node
        const core::InternedId local_node_id = scope_id.path().back();
        const bp2::Blueprint::Node* node = local_node_id.empty() ? nullptr : bp->find_node(local_node_id);
        if (!node || !node->is_blueprint_instance() || !node->has_referenced_blueprint()) {
            return false;
        }

        if (!window.external_blueprint.has_value()) {
            return false;
        }

        const std::string expected_blueprint_id =
            std::string(bp_interner->resolve(node->blueprint_instance().source.blueprint_id()));
        const std::string actual_blueprint_id =
            std::string(window.rendered_interner().resolve(window.external_blueprint->id()));
        return expected_blueprint_id == actual_blueprint_id;
    }

    std::pair<const bp2::Blueprint*, const core::StringInterner*> resolve_parent_blueprint_for_child_scope(
        std::span<const core::InternedId> child_scope_path) const {
        if (child_scope_path.empty()) {
            return {nullptr, nullptr};
        }

        if (child_scope_path.size() == 1) {
            return {&model_.current(), &interner_};
        }

        const std::vector<core::InternedId> parent_path(child_scope_path.begin(), child_scope_path.end() - 1);

        if (const BlueprintWindow* external_parent = find(WindowScopeId::external(parent_path))) {
            return {&external_parent->rendered_blueprint(), &external_parent->rendered_interner()};
        }

        const bp2::Blueprint* embedded_parent =
            editor::resolve_embedded_blueprint(model_.current(), parent_path);
        if (!embedded_parent) {
            return {nullptr, nullptr};
        }
        return {embedded_parent, &interner_};
    }

    BlueprintWindow::Context context() const {
        return BlueprintWindow::Context{model_, interner_, arena_, type_registry_, icon_font_};
    }

    bp2::EditorModel& model_;
    core::StringInterner& interner_;
    bp2::PathArena& arena_;
    const ComponentRegistry* type_registry_ = nullptr;
    const editor::IconFont* icon_font_ = nullptr;
    std::vector<std::unique_ptr<BlueprintWindow>> windows_;
};
