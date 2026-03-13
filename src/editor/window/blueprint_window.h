#pragma once

#include "visual/scene.h"
#include "visual/scene_mutations.h"
#include "viewport/viewport.h"
#include "input/canvas_input.h"
#include "data/blueprint.h"
#include <string>

/// A single editor window showing one nesting level of a blueprint.
/// Each window has its own viewport, visual scene, and input handler
/// — but shares a single Blueprint with other windows.
struct BlueprintWindow {
    std::string title;       ///< ImGui window title
    std::string group_id;    ///< Which collapsed group this window shows ("" = root)
    bool open = true;        ///< Whether the window is open (closable by user)

    Blueprint& bp;           ///< Shared blueprint data (non-owning)
    visual::Scene scene;     ///< Widget-based visual scene
    Viewport viewport;       ///< Pan / zoom / grid state
    CanvasInput input;       ///< FSM-based input handler

    /// Whether the window is read-only (non-baked-in sub-blueprints).
    /// Synced to CanvasInput::read_only so the FSM enforces it.
    bool read_only = false;
    void set_read_only(bool v) { read_only = v; input.read_only = v; }

    /// Construct a window viewing a specific group of a shared blueprint.
    BlueprintWindow(Blueprint& bp_, const std::string& group_id_,
                    const std::string& title_)
        : title(title_)
        , group_id(group_id_)
        , bp(bp_)
        , scene()
        , viewport()
        , input(scene, viewport, bp_, group_id)
    {
        // Sync viewport grid_step from blueprint
        viewport.grid_step = bp_.grid_step;
        // Build initial visual widgets from blueprint data
        visual::mutations::rebuild(scene, bp_, group_id_);
    }

    // Non-copyable, non-movable (contains references)
    BlueprintWindow(const BlueprintWindow&) = delete;
    BlueprintWindow& operator=(const BlueprintWindow&) = delete;
    BlueprintWindow(BlueprintWindow&&) = delete;
    BlueprintWindow& operator=(BlueprintWindow&&) = delete;
};
