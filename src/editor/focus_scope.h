#pragma once

class Document;
struct BlueprintWindow;

/// Identifies which blueprint window the menu bar currently operates on.
///
/// Updated by the rendering layer (SubWindowRenderer, DocumentArea) each frame
/// via ImGui focus detection. Consumed by MainMenu to adapt its content.
///
/// One-frame delay: rendering writes focus during frame N; menu reads it
/// during frame N+1 (16.7ms at 60Hz — imperceptible).
struct FocusScope {
    Document* document = nullptr;
    BlueprintWindow* window = nullptr;

    bool is_root() const;
    bool is_subwindow() const;
    bool is_valid() const;
    bool is_read_only() const;

    void clear() {
        document = nullptr;
        window = nullptr;
    }
};
