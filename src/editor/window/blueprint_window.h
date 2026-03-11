#pragma once

#include "visual/scene/scene.h"
#include "visual/scene/wire_manager.h"
#include "input/canvas_input.h"
#include <string>

/// A single editor window showing one nesting level of a blueprint.
/// Each window has its own viewport, visual cache, input handler,
/// and wire manager — but shares a single Blueprint with other windows.
struct BlueprintWindow {
    std::string title;       ///< ImGui window title
    std::string group_id;    ///< Which collapsed group this window shows ("" = root)
    bool open = true;        ///< Whether the window is open (closable by user)

    VisualScene scene;
    WireManager wire_manager;
    CanvasInput input;

    /// Whether the window is read-only (non-baked-in sub-blueprints).
    /// Synced to CanvasInput::read_only so the FSM enforces it.
    bool read_only = false;
    void set_read_only(bool v) { read_only = v; input.read_only = v; }

    /// Construct a window viewing a specific group of a shared blueprint.
    BlueprintWindow(Blueprint& bp, const std::string& group_id_,
                    const std::string& title_)
        : title(title_)
        , group_id(group_id_)
        , scene(bp, group_id_)
        , wire_manager(scene)
        , input(scene, wire_manager) {}

    // Non-copyable, non-movable (contains references)
    BlueprintWindow(const BlueprintWindow&) = delete;
    BlueprintWindow& operator=(const BlueprintWindow&) = delete;
    BlueprintWindow(BlueprintWindow&&) = delete;
    BlueprintWindow& operator=(BlueprintWindow&&) = delete;
};
