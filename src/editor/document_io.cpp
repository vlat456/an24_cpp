#include "document.h"

#include "core/model/component_registry.h"
#include "blueprint_v2/library/library_path.h"
#include "visual/persist.h"
#include "visual/workspace_session_persist.h"
#include "visual/scene_mutations.h"

#include <spdlog/spdlog.h>

WorkspaceSession Document::captureWorkspaceSession() const {
    WorkspaceSession session;
    session.viewport_pan_x = root().viewport.pan.x;
    session.viewport_pan_y = root().viewport.pan.y;
    session.viewport_zoom = root().viewport.zoom;
    session.grid_step = root().viewport.grid_step;

    for (const auto& win : window_manager_.windows()) {
        if (!win->resolved_scope_id().is_root() && win->open) {
            session.open_windows.push_back(PersistedWindowScope{
                win->resolved_scope_id().mode(),
                win->resolved_scope_id().key(),
            });
        }
    }

    for (const auto& [key, color] : session_node_appearance_) {
        WorkspaceSession::PersistedNodeColor persisted;
        persisted.node_id = std::string(interner_.resolve(key.local_node_id));
        persisted.color = color;
        persisted.instance_path.reserve(key.instance_path.size());
        for (const ui::InternedId segment : key.instance_path) {
            persisted.instance_path.push_back(std::string(interner_.resolve(segment)));
        }
        session.node_colors.push_back(std::move(persisted));
    }

    return session;
}

void Document::applyWorkspaceSession(const WorkspaceSession& session) {
    root().viewport.pan.x = session.viewport_pan_x;
    root().viewport.pan.y = session.viewport_pan_y;
    root().viewport.zoom = session.viewport_zoom;
    root().viewport.grid_step = session.grid_step;
    root().viewport.clamp_zoom();

    for (const auto& window_scope : session.open_windows) {
        if (window_scope.mode == BlueprintWindowMode::EmbeddedScope) {
            const ui::InternedId iid = interner_.lookup(window_scope.key);
            const bp2::Blueprint::Node* node = iid.empty() ? nullptr : model_.current().find_node(iid);
            if (!node || !node->has_embedded_blueprint()) {
                continue;
            }
            auto [win, created] = window_manager_.open(WindowScopeId::embedded(window_scope.key),
                                                       std::string(interner_.resolve(node->semantic.type)) + " [" + window_scope.key + "]");
            if (win && created) {
                win->set_read_only(false);
                win->pending_auto_fit = true;
            }
            continue;
        }

        if (window_scope.mode == BlueprintWindowMode::ExternalReference && library_index_) {
            const ui::InternedId iid = interner_.lookup(window_scope.key);
            const bp2::Blueprint::Node* node = iid.empty() ? nullptr : model_.current().find_node(iid);
            if (!node || !node->has_referenced_blueprint()) {
                continue;
            }
            auto path = bp2::resolve_library_blueprint_path(
                *library_index_,
                std::string(interner_.resolve(node->blueprint_instance().source.blueprint_id())));
            if (!path.has_value()) {
                continue;
            }
            openExternalRefWindow(window_scope.key, *path);
        }
    }

    session_node_appearance_.clear();
    for (const auto& persisted : session.node_colors) {
        editor::NodeInstanceKey key;
        key.local_node_id = interner_.intern(persisted.node_id);
        key.instance_path.reserve(persisted.instance_path.size());
        for (const std::string& segment : persisted.instance_path) {
            key.instance_path.push_back(interner_.intern(segment));
        }
        session_node_appearance_.insert_or_assign(std::move(key), persisted.color);
    }

    // Rebuild all open window scenes to push restored session colors
    // and runtime state into live widgets. Always rebuild — even without
    // colors, reopened windows need seeded runtime/editor state.
    rebuild_window_scenes();
}

bool Document::save(const std::string& path) {
    if (!type_registry_) {
        spdlog::error("[persist] ComponentRegistry is not configured on Document::save");
        return false;
    }

    std::string validation_error;
    if (!validate_blueprint_for_persist(model_.current(), interner_, arena_, *type_registry_, &validation_error)) {
        spdlog::error("[persist] Refusing to save invalid blueprint '{}': {}", path, validation_error);
        return false;
    }

    if (!save_blueprint_to_file(model_.current(), interner_, arena_, *type_registry_, path.c_str())) {
        return false;
    }

    filepath_ = path;
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    model_.mark_saved();
    return true;
}

bool Document::load(const std::string& path) {
    if (!type_registry_) {
        spdlog::error("[persist] ComponentRegistry is not configured on Document::load");
        return false;
    }

    auto bp = load_blueprint_from_file_validated(path.c_str(), interner_, arena_, *type_registry_);
    if (!bp.has_value()) {
        return false;
    }

    window_manager_.close_all();
    root().input.cancel_gesture();

    if (simulation_running_) {
        simulation_.stop();
        simulation_running_ = false;
    }

    // Clear all editor-owned state from the previous document.
    runtime_node_states_.clear();
    session_node_appearance_.clear();

    {
        bp2::EditorModel fresh(std::move(*bp));
        model_ = std::move(fresh);
        sync_next_wire_id();
        model_.mark_saved();
    }

    viewport() = Viewport{};

    // Normalize legacy auto-sized node dimensions to the current layout
    // minimums, but preserve nodes explicitly marked as manually sized.
    // This is a load-time migration, so it must not create undo history
    // or trigger a full rebuild/simulation restart.
    this->apply_normalized_node_sizes(true, false, false);

    ComponentRegistry empty_reg;
    const ComponentRegistry& reg = type_registry_ ? *type_registry_ : empty_reg;
    resetNodeContent(reg);

    // Single authoritative scene rebuild for the root window.  Sub-windows
    // were closed above, so only the root needs rebuilding here.
    visual::mutations::rebuild(scene(), model_.current(), interner_, arena_, root().resolved_scope_id().sim_scope_prefix(), reg,
                               &runtime_node_states_, &session_node_appearance_);
    root().input.rebuild_snapshot();

    filepath_ = path;
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    return true;
}

void Document::sync_next_wire_id() {
    int max_seen = -1;
    for (const auto& w : model_.current().wires()) {
        std::string_view wid = interner_.resolve(w.id);
        if (wid.size() <= 5 || wid.substr(0, 5) != "wire_") {
            continue;
        }
        int n = 0;
        bool ok = true;
        for (size_t i = 5; i < wid.size(); ++i) {
            char c = wid[i];
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
            n = n * 10 + (c - '0');
        }
        if (ok && n > max_seen) {
            max_seen = n;
        }
    }
    model_.next_wire_id_ = max_seen + 1;
}

// ============================================================================
// Workspace/Session Persistence (separate from blueprint)
// ============================================================================

bool Document::saveWorkspaceSession() {
    if (filepath_.empty()) {
        return false;  // No filepath, cannot derive workspace path
    }
    return save_workspace_session(captureWorkspaceSession(), filepath_.c_str());
}

bool Document::loadWorkspaceSession() {
    if (filepath_.empty()) {
        return false;  // No filepath, cannot derive workspace path
    }
    auto ws = load_workspace_session(filepath_.c_str());
    if (!ws.has_value()) {
        return false;
    }
    applyWorkspaceSession(*ws);
    return true;
}
