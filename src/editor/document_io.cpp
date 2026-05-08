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
            std::vector<std::string> path_segments;
            path_segments.reserve(win->resolved_scope_id().path().size());
            for (const auto& seg : win->resolved_scope_id().path()) {
                path_segments.push_back(std::string(interner_.resolve(seg)));
            }
            session.open_windows.push_back(PersistedWindowScope{
                win->resolved_scope_id().mode(),
                std::move(path_segments),
            });
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

    for (const auto& window_scope : session.open_windows) {
        if (window_scope.mode == BlueprintWindowMode::EmbeddedScope) {
            if (window_scope.path_segments.empty()) {
                continue;
            }

            // Validate the full embedded path exists before opening the window.
            auto interned_path = editor::intern_scope_path(interner_, window_scope.path_segments);
            if (!interned_path) {
                continue;
            }
            const WindowScopeId scope_id = WindowScopeId::embedded(*std::move(interned_path));

            // Resolve the host node for the title.
            const editor::ResolvedEmbeddedNode resolved = editor::resolve_embedded_node(
                model_.current(), scope_id.path());
            std::string title = editor::instance_path_to_scope_string(interner_, scope_id.path());
            if (resolved.node && !resolved.node->view.name.empty()) {
                title = resolved.node->view.name + " [" + editor::instance_path_to_scope_string(interner_, scope_id.path()) + "]";
            }

            auto [win, created] = window_manager_.open(scope_id, title);
            if (win && created) {
                win->set_read_only(false);
                win->pending_auto_fit = true;
            }
            continue;
        }

        if (window_scope.mode == BlueprintWindowMode::ExternalReference && library_index_) {
            if (window_scope.path_segments.empty()) {
                continue;
            }
            auto interned_path = editor::intern_scope_path(interner_, window_scope.path_segments);
            if (!interned_path) {
                continue;
            }
            const WindowScopeId scope_id = WindowScopeId::external(*std::move(interned_path));
            // Resolve by full path — the last segment is the reference node id in its
            // immediate parent blueprint, which itself may be nested.
            const editor::ResolvedEmbeddedNode resolved = editor::resolve_embedded_node(
                model_.current(), scope_id.path());
            if (!resolved.node || !resolved.node->has_referenced_blueprint()) {
                continue;
            }
            auto path = bp2::resolve_library_blueprint_path(
                *library_index_,
                std::string(interner_.resolve(resolved.node->blueprint_instance().source.blueprint_id())));
            if (!path.has_value()) {
                continue;
            }
            openExternalRefWindow(scope_id, *path);
        }
    }

    // Rebuild all open window scenes to push runtime/editor state into live widgets.
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

    if (isSimulationRunning()) {
        stopSimulation();
    }

    // Clear all editor-owned state from the previous document.
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

    ComponentRegistry const empty_reg;
    const ComponentRegistry& reg = type_registry_ ? *type_registry_ : empty_reg;
    resetNodeContent(reg);

    // Single authoritative scene rebuild for the root window.  Sub-windows
    // were closed above, so only the root needs rebuilding here.
    const editor::IconFont* icon_font = this->icon_font();
    visual::mutations::rebuild(scene(), model_.current(), interner_, arena_, std::span<const core::InternedId>{}, reg,
                               &runtime_node_states(), icon_font);
    root().input.rebuild_snapshot();

    filepath_ = path;
    auto pos = path.find_last_of("/\\");
    display_name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    return true;
}

void Document::sync_next_wire_id() {
    int max_seen = -1;
    for (const auto& w : model_.current().wires()) {
        std::string_view const wid = interner_.resolve(w.id);
        if (wid.size() <= 5 || wid.substr(0, 5) != "wire_") {
            continue;
        }
        int n = 0;
        bool ok = true;
        for (size_t i = 5; i < wid.size(); ++i) {
            char const c = wid[i];
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
