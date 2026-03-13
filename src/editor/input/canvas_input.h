#pragma once

#include "input/input_types.h"
#include "ui/math/pt.h"
#include "data/port.h"
#include <optional>
#include <string>
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
struct Blueprint;

/// Unified canvas input handler — one per editor window.
/// Owns selection + FSM state, processes raw mouse/key events.
/// Returns InputResult so the host can perform app-level actions
/// (rebuild simulation, open sub-window, show context menu).
///
/// Selection is tracked by widget ID strings, resolved to pointers
/// via the scene's O(1) index only when needed. This eliminates
/// dangling pointer bugs when widgets are destroyed and recreated.
///
/// When read_only is true, only non-destructive operations are allowed:
/// panning, zooming, selection (for inspection), double-click to open
/// sub-windows, and right-click context menus. Node dragging, wire
/// creation/reconnection, deletion, resize, marquee, and routing-point
/// manipulation are all suppressed.
class CanvasInput {
public:
    CanvasInput(visual::Scene& scene, Viewport& viewport,
                Blueprint& bp, const std::string& group_id);

    /// When true, the FSM suppresses all editing gestures.
    bool read_only = false;

    // ---- Event handlers (call from ImGui loop) ----

    InputResult on_mouse_down(Pt screen_pos, MouseButton btn, Pt canvas_min, Modifiers mods = {});
    InputResult on_mouse_up(MouseButton btn, Pt screen_pos, Pt canvas_min);
    InputResult on_mouse_drag(MouseButton btn, Pt screen_delta, Pt canvas_min);
    InputResult on_scroll(float delta, Pt screen_pos, Pt canvas_min);
    InputResult on_double_click(Pt screen_pos, Pt canvas_min);
    InputResult on_key(Key key);

    // ---- Read-only state for rendering ----

    InputState state() const { return state_; }

    /// Selected node IDs.
    const std::vector<std::string>& selected_node_ids() const { return selected_node_ids_; }

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
    bool selectNodeById(const std::string& node_id);

    // ---- Hover tracking ----

    /// Update hover state based on current mouse position (call every frame)
    void update_hover(Pt world_pos);

private:
    visual::Scene& scene_;
    Viewport& viewport_;
    Blueprint& bp_;
    const std::string& group_id_;

    InputState state_ = InputState::Idle;

    // Selection — stored as IDs, resolved via scene.find() when needed.
    std::vector<std::string> selected_node_ids_;
    std::string selected_wire_id_;
    std::string hovered_wire_id_;

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

    // Routing-point drag — transient (pointers valid only during DraggingRoutingPoint)
    std::string rp_wire_id_;
    visual::RoutingPoint* rp_point_ = nullptr;
    size_t rp_index_ = 0;

    // Resize drag — stored as ID
    std::string resize_widget_id_;
    ResizeCorner resize_corner_ = ResizeCorner::BottomRight;
    Pt resize_original_pos_;
    Pt resize_original_size_;

    // Marquee
    Pt marquee_start_;
    Pt marquee_end_;

    // Last known world-space cursor (updated on every event)
    Pt last_world_pos_;

    // ---- Internal helpers ----

    /// Resolve a wire ID to a visual::Wire* (nullptr if not found).
    visual::Wire* resolve_wire(const std::string& id) const;

    /// Resolve a node ID to a visual::Widget* (nullptr if not found).
    visual::Widget* resolve_node(const std::string& id) const;

    // ---- Internal transition helpers ----
    void enter_panning();
    void enter_drag_node(visual::Widget* widget, bool add_to_selection, bool ctrl);
    void enter_drag_routing_point(visual::Wire* wire, visual::RoutingPoint* rp, size_t rp_idx);
    void enter_resize_node(visual::Widget* widget, ResizeCorner corner);
    void enter_create_wire(visual::Port* port, Pt port_pos);
    void enter_reconnect_wire(size_t wire_idx, bool detach_start,
                              Pt anchor_pos, PortSide fixed_side);
    void enter_marquee(Pt world_pos);
    void leave_state();  // return to Idle (clean up transient data)

    InputResult finish_wire_creation(Pt screen_pos, Pt canvas_min);
    InputResult finish_wire_reconnection(Pt screen_pos, Pt canvas_min);
    void finish_marquee();

    // ---- Utility ----

    /// Find the data-layer index of a wire by its visual ID.
    size_t find_wire_index(const std::string& wire_id) const;

    /// Find the data-layer index of a node by its widget ID.
    size_t find_node_index(const std::string& node_id) const;

    /// Look up the data-layer wire index for a port (for reconnection).
    struct WirePortMatch {
        size_t wire_index;
        bool detach_start;
        Pt anchor_pos;
        PortSide fixed_side;
    };
    std::optional<WirePortMatch> find_wire_on_port(visual::Port* port) const;
};
