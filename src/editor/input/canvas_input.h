#pragma once

#include "editor/input/input_types.h"
#include "editor/input/editing_host.h"
#include "editor/visual/presentation/canvas_scene_snapshot.h"
#include "editor/visual/presentation/semantic_canvas_controller.h"
#include "editor/visual/render_context.h"
#include "editor/visual/node/bounds.h"
#include "ui/math/pt.h"
#include "ui/core/interned_id.h"
#include "blueprint_v2/blueprint/node_port.h"
#include "core/model/component_registry.h"
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
} // namespace visual

struct Viewport;
struct ComponentRegistry;

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
                EditingHost& host, ui::StringInterner& interner,
                bp2::PathArena& arena, const std::string& scope_id,
                const ComponentRegistry* parser_registry = nullptr);

    void set_parser_registry(const ComponentRegistry* parser_registry) {
        parser_registry_ = parser_registry;
    }

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

    /// Selected node ids resolved to stable string_views for rendering.
    std::vector<std::string_view> selected_node_id_views() const;

    /// Selected wire id for rendering (empty = none).
    std::string_view selected_wire_id() const;

    /// Hovered wire id for rendering (empty = none).
    std::string_view hovered_wire_id() const;

    /// Semantic identifier of the hovered routing point, for rendering.
    /// Uses wire-id + child-index instead of a raw widget pointer.
    visual::HoveredRoutingPointId hovered_routing_point_id() const { return hovered_rp_id_; }

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
    void add_node_selection(ui::InternedId node_id);
    bool is_node_selected(ui::InternedId node_id) const;

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
    editor::presentation::CanvasSceneSnapshot snapshot_;
    Viewport& viewport_;
    EditingHost& host_;
    ui::StringInterner& interner_;
    bp2::PathArena& arena_;
    const ComponentRegistry* parser_registry_ = nullptr;
    ui::InternedId group_iid_;  // interned handle for O(1) comparisons
    std::string_view scope_id_;  // resolved from interner (stable storage)
    
    // Initial positions for drag-to-command commit (from blueprint data at drag start).
    std::vector<Pt> drag_initial_positions_;
    // Current positions maintained by handle_drag_node (arithmetic, no widget readback).
    // Empty until the first actual drag event; commit_drag_node checks this.
    std::vector<Pt> drag_current_positions_;

    InputState state_ = InputState::Idle;

    // Selection — stored as InternedId handles, resolved via scene.find() when needed.
    std::vector<ui::InternedId> selected_node_ids_;
    ui::InternedId selected_wire_id_;
    ui::InternedId hovered_wire_id_;

    // Hover — routing point identified by wire id + child index (semantic, no raw pointer).
    visual::HoveredRoutingPointId hovered_rp_id_;

    // Drag state (shared by DraggingNode / DraggingRoutingPoint)
    Pt drag_anchor_;
    std::vector<Pt> drag_offsets_;

    struct WireStartEndpoint {
        ui::InternedId node_id;
        ui::InternedId port_id;
        bp2::Direction direction = bp2::Direction::Input;
        PortType type = PortType::Any;
    };

    // Wire creation — semantic endpoint metadata only, no widget pointer.
    std::optional<WireStartEndpoint> wire_start_endpoint_;
    Pt wire_start_pos_;

    // Wire reconnection
    size_t reconnect_wire_idx_ = 0;
    bool reconnect_detach_start_ = false;
    Pt reconnect_anchor_pos_;
    bp2::Direction reconnect_fixed_direction_ = bp2::Direction::Input;
    PortType reconnect_fixed_type_ = PortType::Any;

    // Routing-point drag — semantic wire/id state only.
    ui::InternedId rp_wire_id_;
    size_t rp_index_ = 0;
    Pt rp_drag_pos_{};
    std::vector<Pt> rp_initial_points_;  // snapshot of routing_points at drag start

    // Resize drag — stored as InternedId
    ui::InternedId resize_widget_id_;
    ResizeCorner resize_corner_ = ResizeCorner::BottomRight;
    Pt resize_original_pos_;
    Pt resize_original_size_;
    Pt resize_current_pos_;
    Pt resize_current_size_;

    editor::presentation::SemanticCanvasController semantic_canvas_controller_;
    
    struct SemanticSessionSeed {
        ui::InternedId node_id;
        Pt node_world_pos{};
        Bounds content_bounds{};
        editor::presentation::SemanticSceneSnapshot content_snapshot;
    };
    std::optional<SemanticSessionSeed> semantic_session_seed_;

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
    void enter_drag_node(ui::InternedId node_id, Pt world_pos, bool ctrl);
    void enter_drag_routing_point(ui::InternedId wire_id, size_t rp_idx, Pt rp_world_pos);
    void enter_resize_node(ui::InternedId node_id, Pt world_pos, Pt size, ResizeCorner corner);
    void enter_create_wire(ui::InternedId node_id, ui::InternedId port_id,
                           bp2::Direction direction, PortType type, Pt port_pos);
    void enter_reconnect_wire(size_t wire_idx, bool detach_start,
                              Pt anchor_pos, bp2::Direction fixed_direction, PortType fixed_type);
      void enter_marquee(Pt world_pos);
     void leave_state();  // return to Idle (clean up transient data)

public:
    std::string_view scope_id_for_test() const { return scope_id_; }

    /// Refresh the retained canvas snapshot from the current scene state.
    void rebuild_snapshot();

    /// Cancel any in-flight gesture, clearing all transient pointers.
    /// Call this BEFORE any scene rebuild (undo/redo, node deletion, etc.)
    /// to prevent dangling pointers to destroyed widgets.
    void cancel_gesture();

private:
    enum class SemanticContentRole {
        Toggle,
        DiscreteSelector,
        ContinuousScalar,
    };

    struct SemanticContentTarget {
        SemanticContentRole role = SemanticContentRole::Toggle;
        float primary_min = 0.0f;
        float primary_max = 0.0f;
        int steps = 2;
    };


    InputResult finish_wire_creation(Pt screen_pos, Pt canvas_min);
    InputResult finish_wire_reconnection(Pt screen_pos, Pt canvas_min);
    void finish_marquee();
    /// Handle an already-resolved interaction target. Returns true if interaction was consumed.
    bool handle_resolved_interaction(const visual::HitNode& node_hit,
                                     const SemanticContentTarget& target,
                                     Pt world,
                                     InputResult& result);

     /// Configure semantic snapshot and controller state for interaction role.
       void setup_semantic_interaction_state(const visual::HitNode& node_hit,
                                             const SemanticContentTarget& target,
                                             Pt world_pos);
     
     /// Configure and dispatch semantic interaction based on role. Returns result.
        editor::presentation::SemanticCanvasControllerResult configure_and_dispatch_semantic_interaction(
          const visual::HitNode& node_hit, const SemanticContentTarget& target, Pt world);

      bool publish_semantic_control_result(const editor::presentation::SemanticCanvasControllerResult& semantic,
                                           InputResult& result) const;

     bool state_uses_semantic_control_session() const;

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

    /// Rebuild the visual scene from the current blueprint, then refresh the snapshot.
    void rebuild_scene();

    /// Return the registry reference, safe even when parser_registry_ is null.
    const ComponentRegistry& registry() const;

    /// Find the data-layer index of a wire by its InternedId.
    size_t find_wire_index(ui::InternedId wire_id) const;

    /// Find the data-layer index of a node by its InternedId.
    size_t find_node_index(ui::InternedId node_id) const;

    /// Look up the data-layer wire index for a port (for reconnection).
    struct WirePortMatch {
        size_t wire_index;
        bool detach_start;
        Pt anchor_pos;
        bp2::Direction fixed_direction;
        PortType fixed_type;
    };
    std::optional<WirePortMatch> find_wire_on_port(ui::InternedId port_node_id,
                                                   ui::InternedId port_name_id) const;

    /// Build a WirePortMatch for a given wire index and detach direction.
    WirePortMatch build_wire_port_match(size_t wire_index, bool detach_start,
                                        const bp2::Blueprint::Wire& w) const;
};
