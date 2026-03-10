#pragma once

#include "data/pt.h"
#include <string>

/// Mouse buttons
enum class MouseButton {
    Left,
    Middle,
    Right
};

/// Keyboard keys relevant to canvas interaction
enum class Key {
    Escape,
    Delete,
    Backspace,
    S,
    Z,
    R,
    Space,
    LeftBracket,
    RightBracket,
};

/// Modifier keys held during a mouse event
struct Modifiers {
    bool alt = false;
    bool ctrl = false;   // Ctrl or Cmd on macOS
};

/// Resize handle corners
enum class ResizeCorner {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

/// FSM states for canvas mouse interaction.
/// Exactly one state is active per window at any time.
enum class InputState {
    Idle,                  ///< No active gesture
    Panning,               ///< Left-drag on empty space
    DraggingNode,          ///< Left-drag on a node
    DraggingRoutingPoint,  ///< Left-drag on a wire routing point
    CreatingWire,          ///< Left-drag from a port (new wire)
    ReconnectingWire,      ///< Left-drag from existing wire end
    MarqueeSelect,         ///< Alt+left-drag rectangle selection
    ResizingNode,          ///< Left-drag on a resize handle (group nodes)
};

/// Actions the canvas input wants the host (EditorApp) to perform.
/// Returned from every input method; host checks and executes.
struct InputResult {
    bool rebuild_simulation = false;
    bool show_context_menu = false;
    Pt context_menu_pos;
    bool show_node_context_menu = false;    ///< Right-click on node
    size_t context_menu_node_index = 0;     ///< Which node was right-clicked
    std::string open_sub_window;   ///< non-empty = open this collapsed group

    /// Combine results (logical OR of flags)
    InputResult& operator|=(const InputResult& o) {
        rebuild_simulation |= o.rebuild_simulation;
        show_context_menu  |= o.show_context_menu;
        if (!o.open_sub_window.empty()) open_sub_window = o.open_sub_window;
        if (o.show_context_menu) context_menu_pos = o.context_menu_pos;
        if (o.show_node_context_menu) {
            show_node_context_menu = true;
            context_menu_node_index = o.context_menu_node_index;
        }
        return *this;
    }
};
