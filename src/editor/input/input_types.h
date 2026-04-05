#pragma once

#include "ui/math/pt.h"
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
    Y,
    R,
    Space,
    LeftBracket,
    RightBracket,
};

/// Modifier keys held during a mouse event
struct Modifiers {
    bool alt = false;
    bool ctrl = false;   // Ctrl or Cmd on macOS
    bool shift = false;
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
    DraggingSlider,        ///< Left-drag on a slider content widget
    DraggingKnob,          ///< Left-drag on a knob switch widget
};

/// Actions the canvas input wants the host (Document / WindowSystem) to perform.
/// Returned from every input method; host checks and executes.
struct InputResult {
    bool rebuild_simulation = false;
    bool show_context_menu = false;
    ui::Pt context_menu_pos;
    bool show_node_context_menu = false;    ///< Right-click on node
    std::string context_menu_node_id;       ///< ID of the right-clicked node
    std::string open_sub_window;   ///< non-empty = open this collapsed group
    std::string toggle_switch_node_id;  ///< non-empty = toggle this Switch/AZS node
    std::string toggle_probe_wire_id;   ///< non-empty = toggle oscilloscope probe on wire
    bool has_toggle_probe_world_pos = false;
    ui::Pt toggle_probe_world_pos;
    std::string slider_node_id;         ///< non-empty = set this Slider node's value
    float slider_value = 0.0f;          ///< raw value (already mapped from min..max)
    std::string knob_node_id;           ///< non-empty = set this Knob node's position
    int knob_position = 0;              ///< 0-based position index
    bool open_inline_value_editor = false;  ///< true if inline value editor should open
    std::string inline_value_editor_node_id;  ///< non-empty = open inline value editor for a node
    bool has_inline_value_editor_screen_pos = false;
    ui::Pt inline_value_editor_screen_pos;

    /// Combine results (logical OR of flags)
    InputResult& operator|=(const InputResult& o) {
        rebuild_simulation |= o.rebuild_simulation;
        show_context_menu  |= o.show_context_menu;
        if (!o.open_sub_window.empty()) open_sub_window = o.open_sub_window;
        if (!o.toggle_switch_node_id.empty()) toggle_switch_node_id = o.toggle_switch_node_id;
        if (!o.toggle_probe_wire_id.empty()) toggle_probe_wire_id = o.toggle_probe_wire_id;
        if (o.has_toggle_probe_world_pos) {
            has_toggle_probe_world_pos = true;
            toggle_probe_world_pos = o.toggle_probe_world_pos;
        }
        if (!o.slider_node_id.empty()) {
            slider_node_id = o.slider_node_id;
            slider_value = o.slider_value;
        }
        if (!o.knob_node_id.empty()) {
            knob_node_id = o.knob_node_id;
            knob_position = o.knob_position;
        }
        open_inline_value_editor |= o.open_inline_value_editor;
        if (!o.inline_value_editor_node_id.empty()) {
            inline_value_editor_node_id = o.inline_value_editor_node_id;
        }
        if (o.has_inline_value_editor_screen_pos) {
            has_inline_value_editor_screen_pos = true;
            inline_value_editor_screen_pos = o.inline_value_editor_screen_pos;
        }
        if (o.show_context_menu) context_menu_pos = o.context_menu_pos;
        if (o.show_node_context_menu) {
            show_node_context_menu = true;
            context_menu_node_id = o.context_menu_node_id;
        }
        return *this;
    }
};
