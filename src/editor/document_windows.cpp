#include "document.h"

#include "visual/persist.h"
#include "subwindow_open_target.h"
#include "visual/scene_mutations.h"

#include <cmath>
#include <spdlog/spdlog.h>

namespace {

bool has_default_pan_zoom(const bp2::Blueprint& bp) {
    return std::abs(bp.pan_x()) < 1e-6f
        && std::abs(bp.pan_y()) < 1e-6f
        && std::abs(bp.zoom() - 1.0f) < 1e-6f;
}

} // namespace

void Document::openExternalRefWindow(const std::string& instance_id,
                                      const std::string& blueprint_file_path) {
    // Fast path: reactivate existing window without re-loading the blueprint file.
    // This duplicates the find-and-reactivate logic in WindowManager::open intentionally
    // to avoid expensive file I/O for the already-loaded case.
    if (auto* existing = window_manager_.find(WindowScopeId::external(instance_id))) {
        existing->open = true;
        spdlog::info("[editor] Reactivated external-ref window for '{}'", instance_id);
        return;
    }

    auto ext_interner = std::make_unique<ui::StringInterner>();
    auto ext_arena = std::make_unique<bp2::PathArena>(*ext_interner);
    if (!type_registry_) {
        spdlog::error("[editor] Cannot open external ref window: TypeRegistry is not configured");
        return;
    }

    auto bp = load_blueprint_from_file_validated(
        blueprint_file_path.c_str(), *ext_interner, *ext_arena, *type_registry_);
    if (!bp.has_value()) {
        spdlog::error("[editor] Failed to load external blueprint '{}' for instance '{}'",
                      blueprint_file_path, instance_id);
        return;
    }

    std::string title = instance_id;
    auto lookup_id = interner_.lookup(instance_id);
    if (!lookup_id.empty()) {
        const bp2::Blueprint::Node* node = model_.current().find_node(lookup_id);
        if (node && !node->view.name.empty()) {
            title = node->view.name + " [" + instance_id + "]";
        }
    }

    auto [win, created] = window_manager_.open(WindowScopeId::external(instance_id), title);
    if (!win) {
        spdlog::error("[editor] Failed to create external-ref window for '{}'", instance_id);
        return;
    }
    if (!created) {
        spdlog::info("[editor] Reactivated external-ref window for '{}'", instance_id);
        return;
    }

    win->external_blueprint = std::move(*bp);
    win->external_interner = std::move(ext_interner);
    win->external_arena = std::move(ext_arena);
    win->set_read_only(true);
    win->pending_auto_fit = has_default_pan_zoom(*win->external_blueprint);

    visual::mutations::rebuild(win->scene, *win->external_blueprint,
                               *win->external_interner, *win->external_arena, "");

    spdlog::info("[editor] Opened external-ref window for '{}' from '{}'",
                 instance_id, blueprint_file_path);
}

void Document::openSubWindow(const std::string& sub_blueprint_id) {
    const auto target = editor::resolve_subwindow_open_target(model_.current(), interner_, sub_blueprint_id);
    auto lookup_id = interner_.lookup(sub_blueprint_id);
    const bp2::Blueprint::Nested* nested = lookup_id.empty() ? nullptr : model_.current().find_nested(lookup_id);

    if (target.kind == editor::SubWindowOpenTargetKind::EmbeddedNested && nested) {
        std::string type_name = std::string(interner_.resolve(nested->blueprint_id()));
        auto [win, created] = window_manager_.open(WindowScopeId::embedded(sub_blueprint_id),
                                                   type_name + " [" + sub_blueprint_id + "]");
        if (!win) {
            spdlog::error("[editor] Failed to open sub-window '{}'", sub_blueprint_id);
            return;
        }
        if (!created) {
            spdlog::info("[editor] Reactivated sub-window for '{}'", sub_blueprint_id);
            return;
        }

        win->set_read_only(!nested->is_embedded());

        if (target.kind == editor::SubWindowOpenTargetKind::EmbeddedNested && nested->inline_def()) {
            if (has_default_pan_zoom(*nested->inline_def())) {
                win->pending_auto_fit = true;
            } else {
                win->viewport.pan.x = nested->inline_def()->pan_x();
                win->viewport.pan.y = nested->inline_def()->pan_y();
                win->viewport.zoom = nested->inline_def()->zoom();
                win->viewport.grid_step = nested->inline_def()->grid_step();
                win->viewport.clamp_zoom();
            }
        } else {
            win->pending_auto_fit = true;
        }

        spdlog::info("[editor] Opened sub-window for '{}'", sub_blueprint_id);
        return;
    }

    if (target.kind == editor::SubWindowOpenTargetKind::ReferencedNested) {
        if (target.path.empty()) {
            spdlog::error("[editor] Cannot open referenced nested sub-window '{}': missing blueprint path",
                          sub_blueprint_id);
            return;
        }
        openExternalRefWindow(sub_blueprint_id, target.path);
        return;
    }

    if (target.kind == editor::SubWindowOpenTargetKind::ExternalReference) {
        openExternalRefWindow(sub_blueprint_id, target.path);
        return;
    }

    spdlog::error("[editor] Cannot open sub-window: nested '{}' not found", sub_blueprint_id);
}
