#include "document.h"

#include "debug.h"
#include "visual/persist.h"

bool Document::performUndo() {
    if (!model_.can_undo()) return false;

    for (auto& win : window_manager_.windows()) {
        win->input.cancel_gesture();
    }

    const bp2::Blueprint before_undo = model_.current();
    model_.undo();
#ifndef NDEBUG
    if (!type_registry_) {
        spdlog::error("[editor] ComponentRegistry is not configured on Document::performUndo");
        return false;
    }
    const ComponentRegistry& parser_registry = *type_registry_;
    {
        std::string err;
        if (!validate_blueprint_integrity(model_.current(), interner_, arena_, parser_registry, &err)) {
            model_.replace_current(before_undo);
            spdlog::error("[editor] undo rejected by integrity check: {}", err);
            return false;
        }
    }
#endif

    window_manager_.remove_orphaned_windows();
    rebuild_window_scenes();
    rebuildSimulation();
    return true;
}

bool Document::performRedo() {
    if (!model_.can_redo()) return false;

    for (auto& win : window_manager_.windows()) {
        win->input.cancel_gesture();
    }

    const bp2::Blueprint before_redo = model_.current();
    model_.redo();
#ifndef NDEBUG
    if (!type_registry_) {
        spdlog::error("[editor] ComponentRegistry is not configured on Document::performRedo");
        return false;
    }
    const ComponentRegistry& parser_registry = *type_registry_;
    {
        std::string err;
        if (!validate_blueprint_integrity(model_.current(), interner_, arena_, parser_registry, &err)) {
            model_.replace_current(before_redo);
            spdlog::error("[editor] redo rejected by integrity check: {}", err);
            return false;
        }
    }
#endif

    window_manager_.remove_orphaned_windows();
    rebuild_window_scenes();
    rebuildSimulation();
    return true;
}
