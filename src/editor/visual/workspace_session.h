#pragma once

#include <string>
#include <vector>

/// Workspace/session state for a document. NOT persisted in blueprint documents.
/// This is editor-only state: viewport pan/zoom/grid and open subwindows.
struct WorkspaceSession {
    // Root viewport state
    float viewport_pan_x = 0.0f;
    float viewport_pan_y = 0.0f;
    float viewport_zoom = 1.0f;
    float grid_step = 16.0f;

    // Open subwindow instance ids. Embedded and external windows share the same
    // node/instance id authority and are re-opened through normal document flows.
    std::vector<std::string> open_windows;

    WorkspaceSession() = default;
    ~WorkspaceSession() = default;

    /// Returns true if this is the default/empty workspace state
    bool isDefault() const {
        return viewport_pan_x == 0.0f
            && viewport_pan_y == 0.0f
            && viewport_zoom == 1.0f
            && grid_step == 16.0f
            && open_windows.empty();
    }
};
