#include "editor/transient_ui_manager.h"
#include "editor/window_system.h"

void TransientUIManager::close_for_document(const editor::DocumentId& id) {
    for (auto& e : entries_) {
        if (e.owns_document(e.self, id)) {
            e.close(e.self);
        }
    }
}

void TransientUIManager::close_all() {
    for (auto& e : entries_) {
        e.close(e.self);
    }
}

void TransientUIManager::reconcile(WindowSystem& ws) {
    for (auto& e : entries_) {
        if (!e.still_valid(e.self, ws)) {
            e.close(e.self);
        }
    }
}
