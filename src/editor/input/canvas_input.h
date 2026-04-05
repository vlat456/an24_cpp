#pragma once

#include "editor/input/input_types.h"
#include "ui/math/pt.h"
#include "ui/core/interned_id.h"
#include "data/port.h"
#include "commands/commands.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using ui::Pt;

namespace visual {
class Scene;
class Widget;
class Wire;
class Port;
class RoutingPoint;
} // namespace visual

struct Viewport;

/// Unified canvas input handler — one per editor window.
/// Owns selection + FSM state, processes raw mouse/key events.
/// Returns InputResult so the host can perform app-level actions
/// (rebuild simulation, open sub-window, show context menu).
///
/// Selection is tracked by InternedId (4-byte integer handles),
/// resolved to pointers via the scene's O(1) index only when needed.
/// This eliminates dangling pointer bugs when widgets are destroyed
/// and recreated, and avoids std::string allocations in hot paths.
///
/// When read_only is true, only non-destructive operations are allowed:
/// panning, zooming, selection (for inspection), double-click to open
/// sub-windows, and right-click context menus. Node dragging, wire
/// creation/reconnection, deletion, resize, marquee, and routing-point
/// manipulation are all suppressed.
class CanvasInput {
public:
    CanvasInput(visual::Scene& scene, Viewport& viewport,
                bp2::EditorModel& model, ui::StringInterner& interner,
                bp2::PathArena& arena, const std::string& group_id);

    /// When true, the FSM suppresses all editing gestures.
    bool read_only = false;

    /// When true, editing is suppressed but interactive widgets (slider, knob,
    /// toggle) still respond to clicks/drags.  Used during simulation mode so
    /// the user can manipulate controls without accidentally dragging nodes or
    /// creating wires.
    bool simulation_mode = false;

    // ---- Event handlers (call from ImGui loop) ----

    InputResult on_mouse_down(Pt screen_pos, MouseButton btn, Pt canvas_min, Modifiers mods = {});
    InputResult on_mouse_up(MouseButton btn, Pt screen_pos, Pt canvas_min);
    InputResult on_mouse_drag(MouseButton btn, Pt screen_delta, Pt canvas_min);
    InputResult on_scroll(float delta, Pt screen_pos, Pt canvas_min);
    InputResult on_double_click(Pt screen_pos, Pt canvas_min);
    InputResult on_key(Key key);

    // ---- Read-only state for rendering ----

    InputState state() const { return state_; }

    /// Selected node IDs (interned handles — O(1) comparison).
    const std::vector<ui::InternedId>& selected_node_ids() const { return selected_node_ids_; }

    /// Resolve selected node IDs to widget pointers (for rendering).
    /// Returns only widgets that still exist in the scene.
    std::vector<visual::Widget*> selected_nodes() const;

    /// Selected wire widget (resolved from ID), or nullptr.
    visual::Wire* selected_wire() const;

    /// Wire currently under mouse cursor (resolved from ID), or nullptr.
    visual::Wire* hovered_wire() const;

    /// Routing point currently under mouse cursor, or nullptr.
    /// Valid only during Idle/hover state (transient pointer).
    visual::RoutingPoint* hovered_routing_point() const { return hovered_routing_point_; }

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
    void add_node_selection(visual::Widget* w);
    bool is_node_selected(visual::Widget* w) const;

    /// Select a node by its ID and center the viewport on it.
    /// Returns true if found and selected.
    bool select_node_by_id(std::string_view node_id);

    // ---- Hover tracking ----

    /// Update hover state based on current mouse position (call every frame)
    void update_hover(Pt world_pos);
    
    // ---- Undo/Redo ----
    
    /// Take a snapshot and execute a command (mutation).
    void snapshot_and_execute(Command cmd);

private:
    visual::Scene& scene_;
    Viewport& viewport_;
    bp2::EditorModel& model_;
    ui::StringInterner& interner_;
    bp2::PathArena& arena_;
    ui::InternedId group_iid_;  // interned handle for O(1) comparisons
    std::string_view group_id_;  // resolved from interner (stable storage)
    
    // Initial positions for drag-to-command commit
    std::vector<Pt> drag_initial_positions_;

    InputState state_ = InputState::Idle;

    // Selection — stored as InternedId handles, resolved via scene.find() when needed.
    std::vector<ui::InternedId> selected_node_ids_;
    ui::InternedId selected_wire_id_;
    ui::InternedId hovered_wire_id_;

    // Hover — routing point is transient (only valid during current frame).
    visual::RoutingPoint* hovered_routing_point_ = nullptr;

    // Drag state (shared by DraggingNode / DraggingRoutingPoint)
    Pt drag_anchor_;
    std::vector<Pt> drag_offsets_;

    // Wire creation — transient (port pointer valid only during CreatingWire state)
    visual::Port* wire_start_port_ = nullptr;
    Pt wire_start_pos_;

    // Wire reconnection
    size_t reconnect_wire_idx_ = 0;
    bool reconnect_detach_start_ = false;
    Pt reconnect_anchor_pos_;
    PortSide reconnect_fixed_side_ = PortSide::Input;
    PortType reconnect_fixed_type_ = PortType::Any;

    // Routing-point drag — transient (pointers valid only during DraggingRoutingPoint)
    ui::InternedId rp_wire_id_;
    visual::RoutingPoint* rp_point_ = nullptr;
    size_t rp_index_ = 0;
    std::vector<Pt> rp_initial_points_;  // snapshot of routing_points at drag start

    // Resize drag — stored as InternedId
    ui::InternedId resize_widget_id_;
    ResizeCorner resize_corner_ = ResizeCorner::BottomRight;
    Pt resize_original_pos_;
    Pt resize_original_size_;

    // Slider drag — stored as InternedId + cached widget bounds
    ui::InternedId slider_node_id_;
    Pt slider_widget_world_pos_;  ///< world pos of the SliderWidget at drag start
    float slider_widget_width_ = 0.0f;  ///< width of the SliderWidget

    // Knob drag — stored as InternedId + drag start X for delta tracking
    ui::InternedId knob_node_id_;
    float knob_drag_start_x_ = 0.0f;    ///< world X at drag start
    int knob_drag_start_pos_ = 0;        ///< position at drag start
    int knob_num_positions_ = 2;         ///< total positions for this knob

    // Marquee
    Pt marquee_start_;
    Pt marquee_end_;

    // Last known world-space cursor (updated on every event)
    Pt last_world_pos_;

    // ---- Internal helpers ----

    /// Resolve a wire InternedId to a visual::Wire* (nullptr if not found).
    visual::Wire* resolve_wire(ui::InternedId id) const;

    /// Resolve a node InternedId to a visual::Widget* (nullptr if not found).
    visual::Widget* resolve_node(ui::InternedId id) const;

    // ---- Internal transition helpers ----
    void enter_panning();
    void enter_drag_node(visual::Widget* widget, bool add_to_selection, bool ctrl);
    void enter_drag_routing_point(visual::Wire* wire, visual::RoutingPoint* rp, size_t rp_idx);
    void enter_resize_node(visual::Widget* widget, ResizeCorner corner);
    void enter_create_wire(visual::Port* port, Pt port_pos);
    void enter_reconnect_wire(size_t wire_idx, bool detach_start,
                              Pt anchor_pos, PortSide fixed_side, PortType fixed_type);
    void enter_marquee(Pt world_pos);
    void enter_drag_slider(visual::Widget* node_widget, Pt slider_world_pos, float slider_width);
    void enter_drag_knob(visual::Widget* node_widget, Pt world_pos);
    void leave_state();  // return to Idle (clean up transient data)

public:
    /// Cancel any in-flight gesture, clearing all transient pointers.
    /// Call this BEFORE any scene rebuild (undo/redo, node deletion, etc.)
    /// to prevent dangling pointers to destroyed widgets.
    void cancel_gesture();

private:

    InputResult finish_wire_creation(Pt screen_pos, Pt canvas_min);
    InputResult finish_wire_reconnection(Pt screen_pos, Pt canvas_min);
    void finish_marquee();
    bool try_handle_node_interaction(visual::Widget* widget, Pt world, InputResult& result);
    void clear_selection_and_enter_panning();
    void advance_world_cursor(Pt world_delta);
    void snapshot_wire_routing_points(ui::InternedId wire_id,
                                      std::vector<std::pair<float, float>> new_points);

    // ---- Drag sub-handlers (extracted from on_mouse_drag) ----

    /// Handle DraggingNode state: move selected nodes + invalidate connected wires.
    void handle_drag_node(Pt world_delta);

    /// Re-orient a ref/value single-port node toward its connected neighbor.
    void orient_ref_node_port_by_wire_scan(ui::InternedId ref_node_id);

    /// Orient a ref node toward a specific connected node (pre-built map lookup).
    bool orient_ref_node_port_impl(ui::InternedId ref_id, ui::InternedId connected_id);

    /// Handle ResizingNode state: corner-aware resize with min-size enforcement.
    void handle_resize_node(Pt world_delta);

    // ---- Mouse-up commit handlers (extracted from on_mouse_up) ----

    /// Commit dragged node positions to the data layer via CmdMoveNode.
    void commit_drag_node();

    /// Commit dragged routing point position to the data layer via CmdSetRoutingPoints.
    void commit_drag_routing_point();

    /// Commit resized node dimensions to the data layer via CmdResizeNode.
    void commit_resize_node();

    // ---- Utility ----

    /// Find the data-layer index of a wire by its InternedId.
    size_t find_wire_index(ui::InternedId wire_id) const;

    /// Find the data-layer index of a node by its InternedId.
    size_t find_node_index(ui::InternedId node_id) const;

    /// Look up the data-layer wire index for a port (for reconnection).
    struct WirePortMatch {
        size_t wire_index;
        bool detach_start;
        Pt anchor_pos;
        PortSide fixed_side;
        PortType fixed_type;
    };
    std::optional<WirePortMatch> find_wire_on_port(visual::Port* port) const;

    /// Build a WirePortMatch for a given wire index and detach direction.
    WirePortMatch build_wire_port_match(size_t wire_index, bool detach_start,
                                        const bp2::Blueprint::Wire& w) const;
};
