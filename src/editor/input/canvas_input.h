#pragma once

#include "input/input_types.h"
#include "data/pt.h"
#include "data/port.h"
#include <optional>
#include <string>
#include <vector>

class VisualScene;
class WireManager;

/// Unified canvas input handler — one per editor window.
/// Owns selection + FSM state, processes raw mouse/key events.
/// Returns InputResult so the host can perform app-level actions
/// (rebuild simulation, open sub-window, show context menu).
class CanvasInput {
public:
    CanvasInput(VisualScene& scene, WireManager& wire_manager);

    // ---- Event handlers (call from ImGui loop) ----

    InputResult on_mouse_down(Pt screen_pos, MouseButton btn, Pt canvas_min, Modifiers mods = {});
    InputResult on_mouse_up(MouseButton btn, Pt screen_pos, Pt canvas_min);
    InputResult on_mouse_drag(MouseButton btn, Pt screen_delta, Pt canvas_min);
    InputResult on_scroll(float delta, Pt screen_pos, Pt canvas_min);
    InputResult on_double_click(Pt screen_pos, Pt canvas_min);
    InputResult on_key(Key key);

    // ---- Read-only state for rendering ----

    InputState state() const { return state_; }

    const std::vector<size_t>& selected_nodes() const { return selected_nodes_; }
    std::optional<size_t> selected_wire() const { return selected_wire_; }
    std::optional<size_t> hovered_wire() const { return hovered_wire_; }

    bool is_marquee_selecting() const { return state_ == InputState::MarqueeSelect; }
    Pt marquee_start() const { return marquee_start_; }
    Pt marquee_end() const { return marquee_end_; }

    /// Temporary wire being drawn (for rendering feedback)
    bool has_temp_wire() const;
    Pt temp_wire_start() const;
    Pt temp_wire_end_world() const;
    bool is_reconnecting() const { return state_ == InputState::ReconnectingWire; }

    // ---- Selection helpers ----

    void clear_selection();
    void add_node_selection(size_t idx);
    bool is_node_selected(size_t idx) const;

    /// Select a node by its ID and center the viewport on it.
    /// Returns true if found and selected.
    bool selectNodeById(const std::string& node_id);

    // ---- Hover tracking ----

    /// Update hover state based on current mouse position (call every frame)
    void update_hover(Pt world_pos);

private:
    VisualScene& scene_;
    WireManager& wire_mgr_;

    InputState state_ = InputState::Idle;

    // Selection
    std::vector<size_t> selected_nodes_;
    std::optional<size_t> selected_wire_;
    std::optional<size_t> hovered_wire_;  ///< Wire currently under mouse cursor

    // Drag state (shared by DraggingNode / DraggingRoutingPoint)
    Pt drag_anchor_;
    std::vector<Pt> drag_offsets_;

    // Wire creation
    std::string wire_start_node_;
    std::string wire_start_port_;
    PortSide wire_start_side_ = PortSide::Input;
    Pt wire_start_pos_;

    // Wire reconnection
    size_t reconnect_wire_idx_ = 0;
    bool reconnect_detach_start_ = false;
    Pt reconnect_anchor_pos_;
    PortSide reconnect_fixed_side_ = PortSide::Input;

    // Routing-point drag
    size_t rp_wire_ = 0;
    size_t rp_index_ = 0;

    // Resize drag
    size_t resize_node_idx_ = 0;
    ResizeCorner resize_corner_ = ResizeCorner::BottomRight;
    Pt resize_original_pos_;
    Pt resize_original_size_;

    // Marquee
    Pt marquee_start_;
    Pt marquee_end_;

    // Last known world-space cursor (updated on every event)
    Pt last_world_pos_;

    // ---- Internal transition helpers ----
    void enter_panning();
    void enter_drag_node(size_t node_index, bool add_to_selection, bool ctrl);
    void enter_drag_routing_point(size_t wire_idx, size_t rp_idx);
    void enter_resize_node(size_t node_index, ResizeCorner corner);
    void enter_create_wire(const std::string& node_id, const std::string& port_name,
                           PortSide side, Pt port_pos);
    void enter_reconnect_wire(size_t wire_idx, bool detach_start,
                              Pt anchor_pos, PortSide fixed_side);
    void enter_marquee(Pt world_pos);
    void leave_state();  // return to Idle (clean up transient data)

    InputResult finish_wire_creation(Pt screen_pos, Pt canvas_min);
    InputResult finish_wire_reconnection(Pt screen_pos, Pt canvas_min);
    void finish_marquee();
};
