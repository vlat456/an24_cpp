#pragma once

#include "editor/window/window_scope_id.h"
#include "editor/data/node_state.h"

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
    struct PersistedNodeColor {
        std::vector<std::string> instance_path;
        std::string node_id;
        editor::NodeColor color;

        bool operator==(const PersistedNodeColor& other) const {
            return instance_path == other.instance_path
                && node_id == other.node_id
                && color == other.color;
        }
    };

    // Root viewport state
    float viewport_pan_x = 0.0f;
    float viewport_pan_y = 0.0f;
    float viewport_zoom = 1.0f;
    float grid_step = 16.0f;

    // Open subwindow scope identities. These persist typed scope authority
    // rather than flattening to raw strings.
    std::vector<PersistedWindowScope> open_windows;

    // Per-node session appearance state. Not part of canonical blueprint authority.
    std::vector<PersistedNodeColor> node_colors;

    WorkspaceSession() = default;
    ~WorkspaceSession() = default;

    /// Returns true if this is the default/empty workspace state
    bool isDefault() const {
        return viewport_pan_x == 0.0f
            && viewport_pan_y == 0.0f
            && viewport_zoom == 1.0f
            && grid_step == 16.0f
            && open_windows.empty()
            && node_colors.empty();
    }
};
