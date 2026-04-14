#include "document.h"

#include "blueprint_view_hydration.h"
#include "json_parser/json_parser.h"
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
            session.open_windows.push_back(win->resolved_scope_id().key());
        }
    }

    return session;
}

void Document::applyWorkspaceSession(const WorkspaceSession& session) {
    root().viewport.pan.x = session.viewport_pan_x;
    root().viewport.pan.y = session.viewport_pan_y;
    root().viewport.zoom = session.viewport_zoom;
    root().viewport.grid_step = session.grid_step;
    root().viewport.clamp_zoom();

    for (const auto& window_id : session.open_windows) {
        const ui::InternedId iid = interner_.lookup(window_id);
        const bp2::Blueprint::Node* node = iid.empty() ? nullptr : model_.current().find_node(iid);
        if (node && node->has_embedded_blueprint()) {
            auto [win, created] = window_manager_.open(WindowScopeId::embedded(window_id),
                                                       std::string(interner_.resolve(node->semantic.type)) + " [" + window_id + "]");
            if (win && created) {
                win->set_read_only(false);
                win->pending_auto_fit = true;
            }
            continue;
        }
        if (library_index_) {
            openSubWindow(window_id);
        }
    }
}

bool Document::save(const std::string& path) {
    if (!type_registry_) {
        spdlog::error("[persist] TypeRegistry is not configured on Document::save");
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
        spdlog::error("[persist] TypeRegistry is not configured on Document::load");
        return false;
    }

    auto bp = load_hydrated_blueprint_from_file(path.c_str(), interner_, arena_, *type_registry_);
    if (!bp.has_value()) {
        return false;
    }

    window_manager_.close_all();
    root().input.cancel_gesture();

    if (simulation_running_) {
        simulation_.stop();
        simulation_running_ = false;
    }

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

    visual::mutations::rebuild(scene(), model_.current(), interner_, arena_, root().resolved_scope_id().sim_scope_prefix());
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
