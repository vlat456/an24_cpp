#pragma once

#include "editor/identity.h"
#include "editor/window/window_scope_id.h"

class Document;
struct BlueprintWindow;

/// Identifies which blueprint window the menu bar currently operates on.
///
/// Stores value-typed IDs (never dangles). Pointers are resolved lazily
/// via WindowSystem::resolve_focus() which looks up the live objects.
///
/// Updated by the rendering layer (SubWindowRenderer, DocumentArea) each frame
/// via ImGui focus detection. Consumed by MainMenu to adapt its content.
///
/// One-frame delay: rendering writes focus during frame N; menu reads it
/// during frame N+1 (16.7ms at 60Hz — imperceptible).
struct FocusScope {
    editor::DocumentId document_id;
    WindowScopeId scope_id = WindowScopeId::root();

    /// True if both document_id is set.
    bool is_set() const {
        return !document_id.str().empty();
    }

    /// True if scope refers to the root canvas of the document.
    bool is_root_scope() const {
        return is_set() && scope_id.is_root();
    }

    /// True if scope refers to an embedded or external subwindow.
    bool is_subwindow_scope() const {
        return is_set() && !scope_id.is_root();
    }

    void clear() {
        document_id = editor::DocumentId{};
        scope_id = WindowScopeId::root();
    }

    /// Resolved pointers from IDs. Produced by WindowSystem::resolve_focus().
    struct Resolved {
        Document* document = nullptr;
        BlueprintWindow* window = nullptr;

        bool is_valid() const { return document != nullptr && window != nullptr; }
        bool is_root() const;
        bool is_subwindow() const;
        bool is_read_only() const;
    };
};
