#include "document.h"

#include "core/model/component_registry.h"
#include "visual/persist.h"
#include "subwindow_open_target.h"
#include "visual/scene_mutations.h"
#include "embedded_path_utils.h"

#include <spdlog/spdlog.h>

void Document::openExternalRefWindow(const WindowScopeId& instance_scope,
                                      const std::string& blueprint_file_path) {
    // Fast path: reactivate existing window without re-loading the blueprint file.
    // This duplicates the find-and-reactivate logic in WindowManager::open intentionally
    // to avoid expensive file I/O for the already-loaded case.
    if (auto* existing = window_manager_.find(instance_scope)) {
        existing->open = true;
        spdlog::info("[editor] Reactivated external-ref window for '{}'", editor::instance_path_to_scope_string(interner_, instance_scope.path()));
        return;
    }

    auto ext_interner = std::make_unique<ui::StringInterner>();
    auto ext_arena = std::make_unique<bp2::PathArena>(*ext_interner);
    if (!type_registry_) {
        spdlog::error("[editor] Cannot open external ref window: ComponentRegistry is not configured");
        return;
    }

    auto bp = load_blueprint_from_file_validated(
        blueprint_file_path.c_str(), *ext_interner, *ext_arena, *type_registry_);
    if (!bp.has_value()) {
        spdlog::error("[editor] Failed to load external blueprint '{}' for instance '{}'",
                      blueprint_file_path, editor::instance_path_to_scope_string(interner_, instance_scope.path()));
        return;
    }

    std::string title = editor::instance_path_to_scope_string(interner_, instance_scope.path());
    // Resolve the full path so nested external-ref windows use the correct host
    // node title rather than accidentally probing the root blueprint only.
    if (const editor::ResolvedEmbeddedNode resolved = editor::resolve_embedded_node(
            model_.current(), instance_scope.path());
        resolved.node && !resolved.node->view.name.empty()) {
        title = resolved.node->view.name + " [" + editor::instance_path_to_scope_string(interner_, instance_scope.path()) + "]";
    }

    BlueprintWindow::ExternalDocument external_document{
        std::move(*bp),
        std::move(ext_interner),
        std::move(ext_arena),
    };

    auto [win, created] = window_manager_.open_external(
        instance_scope, title, std::move(external_document));
    if (!win) {
        spdlog::error("[editor] Failed to create external-ref window for '{}'", editor::instance_path_to_scope_string(interner_, instance_scope.path()));
        return;
    }
    if (!created) {
        spdlog::info("[editor] Reactivated external-ref window for '{}'", editor::instance_path_to_scope_string(interner_, instance_scope.path()));
        return;
    }

    win->set_read_only(true);
    win->pending_auto_fit = true;

    spdlog::info("[editor] Opened external-ref window for '{}' from '{}'",
                 editor::instance_path_to_scope_string(interner_, instance_scope.path()), blueprint_file_path);
}

void Document::openSubWindow(const WindowScopeId& parent_scope, ui::InternedId local_node_id) {
    openSubWindow(parent_scope.is_root()
        ? WindowScopeId::embedded({local_node_id})
        : parent_scope.append(local_node_id));
}

void Document::openSubWindow(const WindowScopeId& target_scope) {
    const bp2::Blueprint* bp = nullptr;
    const ui::StringInterner* bp_interner = nullptr;

    if (target_scope.is_root()) {
        return;
    }

    if (target_scope.path().size() == 1) {
        bp = &model_.current();
        bp_interner = &interner_;
    } else {
        const std::vector<ui::InternedId> parent_path(
            target_scope.path().begin(), target_scope.path().end() - 1);
        // Parent resolution depends on what actually exists now:
        // - embedded ancestry always resolves through the root document model
        // - already-open external ancestry resolves through external scope
        // Nested external-ref windows opened from an embedded host do NOT yet
        // have an external parent window, so their parent must still resolve as
        // embedded here.
        WindowScopeId external_parent_scope = WindowScopeId::external(parent_path);
        const ResolvedSignalScope external_parent = resolve_signal_scope(external_parent_scope);
        if (external_parent.blueprint && external_parent.interner) {
            bp = external_parent.blueprint;
            bp_interner = external_parent.interner;
        } else {
            WindowScopeId embedded_parent_scope = WindowScopeId::embedded(parent_path);
            const ResolvedSignalScope embedded_parent = resolve_signal_scope(embedded_parent_scope);
            bp = embedded_parent.blueprint;
            bp_interner = embedded_parent.interner;
        }
    }

    if (!bp || !bp_interner) {
        spdlog::error("[editor] Cannot open sub-window '{}': unresolved parent scope", editor::instance_path_to_scope_string(interner_, target_scope.path()));
        return;
    }

    // Handle embedded blueprints directly — no LibraryIndex needed.
    const ui::InternedId local_node_id = target_scope.path().back();
    const bp2::Blueprint::Node* node = local_node_id.empty() ? nullptr : bp->find_node(local_node_id);

    if (node && node->is_blueprint_instance() && node->has_embedded_blueprint()) {
        std::string type_name = std::string(bp_interner->resolve(node->semantic.type));
        auto [win, created] = window_manager_.open(target_scope,
                                                   type_name + " [" + editor::instance_path_to_scope_string(interner_, target_scope.path()) + "]");
        if (!win) {
            spdlog::error("[editor] Failed to open sub-window '{}'", editor::instance_path_to_scope_string(interner_, target_scope.path()));
            return;
        }
        if (!created) {
            spdlog::info("[editor] Reactivated sub-window for '{}'", editor::instance_path_to_scope_string(interner_, target_scope.path()));
            return;
        }

        win->set_read_only(false);
        win->pending_auto_fit = true;

        spdlog::info("[editor] Opened sub-window for '{}'", editor::instance_path_to_scope_string(interner_, target_scope.path()));
        return;
    }

    // Referenced / external blueprints require a LibraryIndex.
    if (!library_index_) {
        spdlog::error("[editor] Cannot open sub-window '{}': LibraryIndex is not configured",
                      editor::instance_path_to_scope_string(interner_, target_scope.path()));
        return;
    }

    const auto result = editor::resolve_subwindow_open_target(
        *bp, *bp_interner, *library_index_, std::string(bp_interner->resolve(local_node_id)));
    const auto& target = result.target;

    if (target.kind == editor::SubWindowOpenTargetKind::EmbeddedNested && node && node->is_blueprint_instance()) {
        // Non-embedded blueprint instance that resolve_subwindow_open_target still
        // classified as EmbeddedNested (e.g. blueprint has source but not
        // has_embedded_blueprint). Open as read-only scope.
        std::string type_name = std::string(bp_interner->resolve(node->semantic.type));
        auto [win, created] = window_manager_.open(target_scope,
                                                   type_name + " [" + editor::instance_path_to_scope_string(interner_, target_scope.path()) + "]");
        if (!win) {
            spdlog::error("[editor] Failed to open sub-window '{}'", editor::instance_path_to_scope_string(interner_, target_scope.path()));
            return;
        }
        if (!created) {
            spdlog::info("[editor] Reactivated sub-window for '{}'", editor::instance_path_to_scope_string(interner_, target_scope.path()));
            return;
        }

        win->set_read_only(!node->has_embedded_blueprint());
        win->pending_auto_fit = true;

        spdlog::info("[editor] Opened sub-window for '{}'", editor::instance_path_to_scope_string(interner_, target_scope.path()));
        return;
    }

    if (target.kind == editor::SubWindowOpenTargetKind::ReferencedNested) {
        if (target.path.empty()) {
            spdlog::error("[editor] Cannot open referenced nested sub-window '{}': missing blueprint path",
                          editor::instance_path_to_scope_string(interner_, target_scope.path()));
            return;
        }
        openExternalRefWindow(WindowScopeId::external(target_scope.path()), target.path);
        return;
    }

    if (target.kind == editor::SubWindowOpenTargetKind::ExternalReference) {
        openExternalRefWindow(WindowScopeId::external(target_scope.path()), target.path);
        return;
    }

    spdlog::error("[editor] Cannot open sub-window '{}': {}",
                  editor::instance_path_to_scope_string(interner_, target_scope.path()),
                  editor::to_string(result.failure));
}
