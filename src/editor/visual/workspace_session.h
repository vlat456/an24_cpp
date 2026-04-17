#pragma once

#include "editor/window/window_scope_id.h"

#include <string>
#include <vector>

struct PersistedWindowScope {
    BlueprintWindowMode mode = BlueprintWindowMode::RootDocument;
    std::string key;

    bool operator==(const PersistedWindowScope& other) const {
        return mode == other.mode && key == other.key;
    }
};

/// Workspace/session state for a document. NOT persisted in blueprint documents.
/// This is editor-only state: viewport pan/zoom/grid and open subwindows.
struct WorkspaceSession {
    // Root viewport state
    float viewport_pan_x = 0.0f;
    float viewport_pan_y = 0.0f;
    float viewport_zoom = 1.0f;
    float grid_step = 16.0f;

    // Open subwindow scope identities. These persist typed scope authority
    // rather than flattening to raw strings.
    std::vector<PersistedWindowScope> open_windows;

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
