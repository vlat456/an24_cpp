#include "input/canvas_input.h"
#include "visual/scene.h"
#include "visual/scene_hittest.h"
#include "visual/scene_mutations.h"
#include "visual/widget.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "visual/port/visual_port.h"
#include "visual/node/group_node_widget.h"
#include "visual/node/visual_node.h"
#include "visual/widgets/content_widgets.h"
#include "visual/snap.h"
#include "viewport/viewport.h"
#include "commands/commands.h"
#include "visual/persist.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "debug.h"
#include <imgui.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <unordered_set>

// ============================================================================
// File-local helpers
// ============================================================================

/// Extract (node_id, port_name) from a bp2 wire endpoint Path.
static std::pair<ui::InternedId, ui::InternedId>
path_to_node_port(const bp2::Path& path, const bp2::PathArena& arena) {
    if (path.kind() != bp2::PathKind::Port) return {};
    ui::InternedId port_name = path.segment();
    bp2::Path parent = arena.parent(path);
    if (parent.kind() != bp2::PathKind::Node) return {};
    return {parent.segment(), port_name};
}

static bool is_bus_node(const bp2::EditorModel& model, ui::InternedId node_id) {
    const bp2::Blueprint::Node* node = model.current().find_node(node_id);
    if (!node) return false;
    return node->render_hint == "bus";
}

static bool is_wire_alias_port_name(std::string_view port_name) {
    return !port_name.empty() && port_name != "v";
}

static PortType resolve_port_type_from_model(const bp2::EditorModel& model,
                                             ui::InternedId node_id,
                                             ui::InternedId port_name) {
    const bp2::Blueprint::Node* node = model.current().find_node(node_id);
    if (!node) return PortType::Any;
    for (const auto& p : node->inputs) {
        if (p.name == port_name) return p.type;
    }
    for (const auto& p : node->outputs) {
        if (p.name == port_name) return p.type;
    }
    return PortType::Any;
}

static void debug_validate_command_boundary(bp2::EditorModel const& model,
                                            ui::StringInterner& interner,
                                            bp2::PathArena const& arena) {
#ifndef NDEBUG
    std::string err;
    const bool ok = validate_blueprint_integrity(model.current(), interner, arena, &err);
    if (!ok) {
        if (err.find("wire domain differs from endpoint domain") != std::string::npos
            || err.find("wire direction incompatible") != std::string::npos
            || err.find("wire endpoint path unresolved") != std::string::npos
            || err.find("wire endpoint domain mismatch") != std::string::npos) {
            // Some editor-only/transient fixtures intentionally omit fully normalized
            // wire semantics; keep structural debug checks without tripping here.
            return;
        }
        std::fprintf(stderr, "[bp2][debug] command boundary invariant failed: %s\n", err.c_str());
        assert(false && "bp2 integrity violation at command boundary");
    }
#else
    (void)model;
    (void)interner;
    (void)arena;
#endif
}

// ============================================================================
// Construction
// ============================================================================

CanvasInput::CanvasInput(visual::Scene& scene, Viewport& viewport,
                         bp2::EditorModel& model, ui::StringInterner& interner,
                         bp2::PathArena& arena, const std::string& group_id)
    : scene_(scene), viewport_(viewport), model_(model),
      interner_(interner), arena_(arena),
      group_iid_(interner.intern(group_id)),
      group_id_(interner.resolve(group_iid_))
{
}

// ============================================================================
// Command execution (checkpoint-based)
// ============================================================================

void CanvasInput::snapshot_and_execute(Command cmd) {
    model_.push_checkpoint();
    execute(model_, interner_, std::move(cmd));
    debug_validate_command_boundary(model_, interner_, arena_);
}

// ============================================================================
// ID → pointer resolution helpers
// ============================================================================

visual::Wire* CanvasInput::resolve_wire(ui::InternedId id) const {
    if (id.empty()) return nullptr;
    return dynamic_cast<visual::Wire*>(scene_.find(interner_.resolve(id)));
}

visual::Widget* CanvasInput::resolve_node(ui::InternedId id) const {
    if (id.empty()) return nullptr;
    return scene_.find(interner_.resolve(id));
}

// ============================================================================
// Selection helpers
// ============================================================================

void CanvasInput::clear_selection() {
    selected_node_ids_.clear();
    selected_wire_id_ = {};
}

void CanvasInput::add_node_selection(visual::Widget* w) {
    if (!w) return;
    ui::InternedId iid = interner_.intern(w->id());
    if (iid.empty()) return;
    if (std::find(selected_node_ids_.begin(), selected_node_ids_.end(), iid) == selected_node_ids_.end())
        selected_node_ids_.push_back(iid);
}

bool CanvasInput::is_node_selected(visual::Widget* w) const {
    if (!w) return false;
    ui::InternedId iid = interner_.lookup(w->id());
    if (iid.empty()) return false;
    return std::find(selected_node_ids_.begin(), selected_node_ids_.end(), iid) != selected_node_ids_.end();
}

std::vector<visual::Widget*> CanvasInput::selected_nodes() const {
    std::vector<visual::Widget*> result;
    result.reserve(selected_node_ids_.size());
    for (const auto& nid : selected_node_ids_) {
        auto* w = resolve_node(nid);
        if (w) result.push_back(w);
    }
    return result;
}

visual::Wire* CanvasInput::selected_wire() const {
    return resolve_wire(selected_wire_id_);
}

visual::Wire* CanvasInput::hovered_wire() const {
    return resolve_wire(hovered_wire_id_);
}

bool CanvasInput::select_node_by_id(std::string_view node_id) {
    ui::InternedId iid = interner_.intern(node_id);
    auto* widget = resolve_node(iid);
    if (!widget) return false;

    clear_selection();
    add_node_selection(widget);

    // Center viewport on the node
    Pt pos = widget->worldPos();
    Pt sz = widget->size();
    Pt center(pos.x + sz.x * 0.5f, pos.y + sz.y * 0.5f);
    viewport_.centerOn(center, 800.0f, 600.0f);
    return true;
}

// ============================================================================
// Hover tracking
// ============================================================================

void CanvasInput::update_hover(Pt world_pos) {
    // Don't update hover during drag operations
    if (state_ == InputState::DraggingNode ||
        state_ == InputState::DraggingRoutingPoint ||
        state_ == InputState::CreatingWire ||
        state_ == InputState::ReconnectingWire ||
        state_ == InputState::ResizingNode ||
        state_ == InputState::DraggingSlider) {
        hovered_wire_id_ = {};
        hovered_routing_point_ = nullptr;
        return;
    }

    // Check for wire/routing-point hover
    auto hit = visual::hit_test(scene_, world_pos);
    if (auto* h = std::get_if<visual::HitWire>(&hit)) {
        hovered_wire_id_ = interner_.intern(h->wire->id());
        hovered_routing_point_ = nullptr;
    } else if (auto* h = std::get_if<visual::HitRoutingPoint>(&hit)) {
        hovered_wire_id_ = interner_.intern(h->wire->id());
        hovered_routing_point_ = h->point;
    } else {
        hovered_wire_id_ = {};
        hovered_routing_point_ = nullptr;
    }
}

// ============================================================================
// Temp-wire queries for rendering
// ============================================================================

bool CanvasInput::has_temp_wire() const {
    return state_ == InputState::CreatingWire || state_ == InputState::ReconnectingWire;
}

Pt CanvasInput::temp_wire_start() const {
    if (state_ == InputState::ReconnectingWire)
        return reconnect_anchor_pos_;
    return wire_start_pos_;
}

Pt CanvasInput::temp_wire_end_world() const {
    return last_world_pos_;
}

// ============================================================================
// State transitions
// ============================================================================

void CanvasInput::enter_panning() {
    state_ = InputState::Panning;
}

void CanvasInput::enter_drag_node(visual::Widget* widget, bool add_to_selection, bool ctrl) {
    if (!ctrl) clear_selection();
    add_node_selection(widget);

    Pt primary_pos = widget->worldPos();

    state_ = InputState::DraggingNode;
    drag_anchor_ = primary_pos;
    drag_offsets_.clear();
    drag_initial_positions_.clear();
    auto nodes = selected_nodes();
    for (auto* sel : nodes) {
        drag_offsets_.push_back(sel->worldPos() - primary_pos);
        drag_initial_positions_.push_back(sel->worldPos());
    }

    // Checkpoint BEFORE any drag mutations happen
    model_.push_checkpoint();
}

void CanvasInput::enter_drag_routing_point(visual::Wire* wire, visual::RoutingPoint* rp, size_t rp_idx) {
    state_ = InputState::DraggingRoutingPoint;
    auto wire_iid = interner_.intern(wire->id());
    selected_wire_id_ = wire_iid;
    rp_wire_id_ = wire_iid;
    rp_point_ = rp;
    rp_index_ = rp_idx;
    drag_anchor_ = rp->worldPos();

    // Snapshot routing points for undo (before any drag mutation)
    const bp2::Blueprint::Wire* bp2_wire = model_.current().find_wire(rp_wire_id_);
    if (bp2_wire) {
        rp_initial_points_.clear();
        rp_initial_points_.reserve(bp2_wire->routing_points.size());
        for (const auto& [rx, ry] : bp2_wire->routing_points)
            rp_initial_points_.push_back(Pt(rx, ry));
    } else {
        rp_initial_points_.clear();
    }

    // Checkpoint BEFORE any drag mutations happen
    model_.push_checkpoint();
}

void CanvasInput::enter_resize_node(visual::Widget* widget, ResizeCorner corner) {
    state_ = InputState::ResizingNode;
    clear_selection();
    add_node_selection(widget);
    resize_widget_id_ = interner_.intern(widget->id());
    resize_corner_ = corner;
    resize_original_pos_ = widget->worldPos();
    resize_original_size_ = widget->size();
    drag_anchor_ = Pt(0, 0);  // accumulated delta

    // Checkpoint BEFORE any resize mutations happen
    model_.push_checkpoint();
}

void CanvasInput::enter_create_wire(visual::Port* port, Pt port_pos) {
    state_ = InputState::CreatingWire;
    wire_start_port_ = port;
    wire_start_pos_ = port_pos;
}

void CanvasInput::enter_reconnect_wire(size_t wire_idx, bool detach_start,
                                       Pt anchor_pos, PortSide fixed_side, PortType fixed_type) {
    state_ = InputState::ReconnectingWire;
    reconnect_wire_idx_ = wire_idx;
    reconnect_detach_start_ = detach_start;
    reconnect_anchor_pos_ = anchor_pos;
    reconnect_fixed_side_ = fixed_side;
    reconnect_fixed_type_ = fixed_type;
}

void CanvasInput::enter_marquee(Pt world_pos) {
    state_ = InputState::MarqueeSelect;
    marquee_start_ = world_pos;
    marquee_end_ = world_pos;
}

void CanvasInput::enter_drag_slider(visual::Widget* node_widget, Pt slider_world_pos, float slider_width) {
    state_ = InputState::DraggingSlider;
    slider_node_id_ = interner_.intern(node_widget->id());
    slider_widget_world_pos_ = slider_world_pos;
    slider_widget_width_ = slider_width;
}

void CanvasInput::leave_state() {
    state_ = InputState::Idle;
    drag_offsets_.clear();
    drag_initial_positions_.clear();
    rp_initial_points_.clear();
    wire_start_port_ = nullptr;
    rp_point_ = nullptr;
    hovered_routing_point_ = nullptr;
}

void CanvasInput::cancel_gesture() {
    // If a drag/resize gesture is in-flight, restore pre-gesture blueprint
    // state. CreatingWire / ReconnectingWire / MarqueeSelect don't take
    // checkpoints so they just need their transient state cleared.
    if (state_ == InputState::DraggingNode ||
        state_ == InputState::DraggingRoutingPoint ||
        state_ == InputState::ResizingNode) {
        model_.undo();
    }
    leave_state();
}

// ============================================================================
// Content toggle check (extracted from on_mouse_down)
// ============================================================================

std::string CanvasInput::check_content_toggle(visual::Widget& widget, Pt world_pos) {
    std::string node_id(widget.id());
    ui::InternedId node_iid = interner_.lookup(node_id);
    const bp2::Blueprint::Node* node = node_iid.empty() ? nullptr
                                                        : model_.current().find_node(node_iid);
    if (!node) return {};
    if (node->content_type != bp2::NodeContentType::Switch &&
        node->content_type != bp2::NodeContentType::VerticalToggle) return {};

    // Only toggle if click is inside the content widget bounds;
    // clicks on header / ports / footer should select/drag instead.
    if (auto* nw = dynamic_cast<visual::NodeWidget*>(&widget)) {
        Bounds cb = nw->contentBounds();
        Pt wpos = nw->worldPos();
        float lx = world_pos.x - wpos.x;
        float ly = world_pos.y - wpos.y;
        if (cb.contains(lx, ly)) return node_id;
    }
    return {};
}

// ============================================================================
// Slider content hit check
// ============================================================================

std::string CanvasInput::check_slider_hit(visual::Widget& widget, Pt world_pos, float& out_local_x) {
    std::string node_id(widget.id());
    ui::InternedId node_iid = interner_.lookup(node_id);
    const bp2::Blueprint::Node* node = node_iid.empty() ? nullptr
                                                        : model_.current().find_node(node_iid);
    if (!node) return {};
    if (node->content_type != bp2::NodeContentType::Slider) return {};

    if (auto* nw = dynamic_cast<visual::NodeWidget*>(&widget)) {
        Bounds cb = nw->contentBounds();
        Pt wpos = nw->worldPos();
        float lx = world_pos.x - wpos.x;
        float ly = world_pos.y - wpos.y;
        if (cb.contains(lx, ly)) {
            // local X within the slider widget itself
            out_local_x = lx - cb.x;
            return node_id;
        }
    }
    return {};
}

// ============================================================================
// on_mouse_down
// ============================================================================

InputResult CanvasInput::on_mouse_down(Pt screen_pos, MouseButton btn, Pt canvas_min, Modifiers mods) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    last_world_pos_ = world;

    if (btn == MouseButton::Left) {
        if (mods.shift) {
            auto hit = visual::hit_test(scene_, world);
            if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
                result.toggle_probe_wire_id = std::string(hw->wire->id());
                result.has_toggle_probe_world_pos = true;
                result.toggle_probe_world_pos = world;
                return result;
            }
        }

        if (read_only) {
            // Read-only: left-click only allows panning and node selection (for inspection)
            auto hit = visual::hit_test(scene_, world);
            if (auto* h = std::get_if<visual::HitNode>(&hit)) {
                if (!mods.ctrl) clear_selection();
                add_node_selection(h->widget);
            } else {
                clear_selection();
                enter_panning();
            }
            return result;
        }

        // 1. Check ports first (wire creation / reconnection)
        auto port_hit = visual::hit_test_ports(scene_, world);
        if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
            auto wire_match = find_wire_on_port(ph->port);
            if (wire_match) {
                enter_reconnect_wire(wire_match->wire_index, wire_match->detach_start,
                                     wire_match->anchor_pos, wire_match->fixed_side,
                                     wire_match->fixed_type);
                return result;
            }
            Pt port_center = ph->port->worldPos() + Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
            enter_create_wire(ph->port, port_center);
            return result;
        }

        // 2. General hit test
        auto hit = visual::hit_test(scene_, world);

        if (mods.alt) {
            // Alt+click → marquee selection
            enter_marquee(world);
        } else if (auto* hrh = std::get_if<visual::HitResizeHandle>(&hit)) {
            // Resize handle on a resizable widget (group, text annotation)
            enter_resize_node(hrh->widget, hrh->corner);
        } else if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
            // Check if click landed on Slider content area → enter drag
            float slider_local_x = 0.0f;
            auto slider_id = check_slider_hit(*hn->widget, world, slider_local_x);
            if (!slider_id.empty()) {
                auto* nw = dynamic_cast<visual::NodeWidget*>(hn->widget);
                if (nw) {
                    Bounds cb = nw->contentBounds();
                    Pt nw_pos = nw->worldPos();
                    Pt slider_wpos(nw_pos.x + cb.x, nw_pos.y + cb.y);
                    enter_drag_slider(hn->widget, slider_wpos, cb.w);

                    // Compute initial value from click position
                    ui::InternedId nid_iid = interner_.lookup(slider_id);
                    const bp2::Blueprint::Node* node = nid_iid.empty() ? nullptr
                                                                       : model_.current().find_node(nid_iid);
                    if (node) {
                        // Inline SliderWidget::normalizedFromLocalX logic
                        float pad = visual::SliderWidget::HANDLE_RADIUS;
                        float track_w = cb.w - 2.0f * pad;
                        float t = (track_w > 0.0f) ? std::clamp((slider_local_x - pad) / track_w, 0.0f, 1.0f) : 0.0f;
                        float val = node->content_min + t * (node->content_max - node->content_min);
                        result.slider_node_id = std::move(slider_id);
                        result.slider_value = val;
                    }
                }
                return result;
            }

            // Check if click landed on Switch/VerticalToggle content area
            auto toggle_id = check_content_toggle(*hn->widget, world);
            if (!toggle_id.empty()) {
                result.toggle_switch_node_id = std::move(toggle_id);
                return result;
            }
            enter_drag_node(hn->widget, false, mods.ctrl);
        } else if (auto* hrp = std::get_if<visual::HitRoutingPoint>(&hit)) {
            enter_drag_routing_point(hrp->wire, hrp->point, hrp->index);
        } else if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
            clear_selection();
            selected_wire_id_ = interner_.intern(hw->wire->id());
        } else {
            // Empty space → panning
            clear_selection();
            enter_panning();
        }
    } else if (btn == MouseButton::Right && !read_only) {
        auto hit = visual::hit_test(scene_, world);
        if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
            result.show_node_context_menu = true;
            result.context_menu_node_id = std::string(hn->widget->id());
        } else if (std::holds_alternative<visual::HitEmpty>(hit)) {
            result.show_context_menu = true;
            result.context_menu_pos = world;
        }
    }
    return result;
}

// ============================================================================
// Drag sub-handlers (extracted from on_mouse_drag)
// ============================================================================

void CanvasInput::handle_drag_node(Pt world_delta) {
    drag_anchor_ = drag_anchor_ + world_delta;
    Pt snapped = editor_math::snap_to_grid(drag_anchor_, viewport_.grid_step);
    auto nodes = selected_nodes();

    // Collect wire IDs connected to any moved node (for grid update).
    // Use a set to avoid duplicating wires shared between two moved nodes.
    std::unordered_set<ui::InternedId> connected_wire_ids;

    for (size_t i = 0; i < nodes.size(); i++) {
        auto* widget = nodes[i];
        Pt offset = (i < drag_offsets_.size()) ? drag_offsets_[i] : Pt(0, 0);
        Pt new_pos = snapped + offset;

        // Update visual widget only (data layer committed on mouse-up via CmdMoveNode)
        widget->setLocalPos(new_pos);

        // Gather connected wire IDs by scanning all bp2 wires
        ui::InternedId node_iid = interner_.intern(widget->id());
        if (!node_iid.empty()) {
            for (const bp2::Blueprint::Wire& w : model_.current().wires()) {
                auto [src_node, src_port] = path_to_node_port(w.source, arena_);
                auto [tgt_node, tgt_port] = path_to_node_port(w.target, arena_);
                if (src_node == node_iid || tgt_node == node_iid) {
                    connected_wire_ids.insert(w.id);
                }
            }
        }

        // Update node widget's Grid entry
        if (widget->isClickable())
            scene_.grid().update(widget);
    }

    // Invalidate geometry + update Grid for connected wires
    for (auto wid : connected_wire_ids) {
        auto* wire = dynamic_cast<visual::Wire*>(
            scene_.find(interner_.resolve(wid)));
        if (wire) {
            wire->invalidateGeometry();
            if (wire->isClickable())
                scene_.grid().update(wire);
        }
    }
}

void CanvasInput::handle_resize_node(Pt world_delta) {
    drag_anchor_ = drag_anchor_ + world_delta;
    auto* resize_widget = resolve_node(resize_widget_id_);
    if (!resize_widget) return;

    float grid = viewport_.grid_step;
    Pt orig_pos = resize_original_pos_;
    Pt orig_sz = resize_original_size_;
    Pt delta = drag_anchor_;
    Pt new_pos = orig_pos;
    Pt new_size = orig_sz;

    switch (resize_corner_) {
        case ResizeCorner::BottomRight:
            new_size = Pt(orig_sz.x + delta.x, orig_sz.y + delta.y);
            break;
        case ResizeCorner::BottomLeft:
            new_pos.x = orig_pos.x + delta.x;
            new_size = Pt(orig_sz.x - delta.x, orig_sz.y + delta.y);
            break;
        case ResizeCorner::TopRight:
            new_pos.y = orig_pos.y + delta.y;
            new_size = Pt(orig_sz.x + delta.x, orig_sz.y - delta.y);
            break;
        case ResizeCorner::TopLeft:
            new_pos = Pt(orig_pos.x + delta.x, orig_pos.y + delta.y);
            new_size = Pt(orig_sz.x - delta.x, orig_sz.y - delta.y);
            break;
    }

    // Enforce minimum size (1 grid unit)
    float min_w = editor_constants::PORT_LAYOUT_GRID;
    float min_h = editor_constants::PORT_LAYOUT_GRID;
    if (new_size.x < min_w) {
        if (resize_corner_ == ResizeCorner::TopLeft || resize_corner_ == ResizeCorner::BottomLeft)
            new_pos.x = orig_pos.x + orig_sz.x - min_w;
        new_size.x = min_w;
    }
    if (new_size.y < min_h) {
        if (resize_corner_ == ResizeCorner::TopLeft || resize_corner_ == ResizeCorner::TopRight)
            new_pos.y = orig_pos.y + orig_sz.y - min_h;
        new_size.y = min_h;
    }

    // Snap to grid
    new_pos = editor_math::snap_to_grid(new_pos, grid);
    new_size = editor_math::snap_to_grid(new_size, grid);

    // Update visual widget only (data layer committed on mouse-up via CmdResizeNode)
    resize_widget->setLocalPos(new_pos);
    resize_widget->layout(new_size.x, new_size.y);
}

// ============================================================================
// on_mouse_drag
// ============================================================================

InputResult CanvasInput::on_mouse_drag(MouseButton btn, Pt screen_delta, Pt canvas_min) {
    InputResult result;
    float zoom = viewport_.zoom;
    Pt world_delta(screen_delta.x / zoom, screen_delta.y / zoom);

    if (btn == MouseButton::Left) {
        switch (state_) {
            case InputState::Panning:
                viewport_.pan.x -= world_delta.x;
                viewport_.pan.y -= world_delta.y;
                last_world_pos_ = last_world_pos_ + world_delta;
                break;

            case InputState::DraggingNode:
                handle_drag_node(world_delta);
                break;

            case InputState::DraggingRoutingPoint: {
                drag_anchor_ = drag_anchor_ + world_delta;
                Pt snapped = editor_math::snap_to_grid(drag_anchor_, viewport_.grid_step);

                // Update visual widget only (data layer committed on mouse-up via CmdSetRoutingPoints)
                if (rp_point_) {
                    rp_point_->setLocalPos(snapped);
                    auto* rp_wire = resolve_wire(rp_wire_id_);
                    if (rp_wire) {
                        rp_wire->invalidateGeometry();
                        // Update Wire's Grid entry too (bounds changed)
                        if (rp_wire->scene() && rp_wire->isClickable())
                            rp_wire->scene()->grid().update(rp_wire);
                    }
                    if (rp_point_->scene())
                        rp_point_->scene()->grid().update(rp_point_);
                }
                break;
            }

            case InputState::CreatingWire:
            case InputState::ReconnectingWire:
                last_world_pos_ = last_world_pos_ + world_delta;
                break;

            case InputState::MarqueeSelect:
                marquee_end_ = marquee_end_ + world_delta;
                break;

            case InputState::ResizingNode:
                handle_resize_node(world_delta);
                break;

            case InputState::DraggingSlider: {
                last_world_pos_ = last_world_pos_ + world_delta;
                // Recompute slider value from current world cursor position
                float local_x = last_world_pos_.x - slider_widget_world_pos_.x;
                float pad = visual::SliderWidget::HANDLE_RADIUS;
                float track_w = slider_widget_width_ - 2.0f * pad;
                float t = (track_w > 0.0f) ? std::clamp((local_x - pad) / track_w, 0.0f, 1.0f) : 0.0f;

                const bp2::Blueprint::Node* node = model_.current().find_node(slider_node_id_);
                if (node) {
                    float val = node->content_min + t * (node->content_max - node->content_min);
                    result.slider_node_id = std::string(interner_.resolve(slider_node_id_));
                    result.slider_value = val;
                }
                break;
            }

            case InputState::Idle:
                break;
        }
    }
    return result;
}

// ============================================================================
// Mouse-up commit handlers (extracted from on_mouse_up)
// ============================================================================

void CanvasInput::commit_drag_node() {
    auto nodes = selected_nodes();
    bool any_moved = false;
    for (size_t i = 0; i < nodes.size() && i < drag_initial_positions_.size(); ++i) {
        if (nodes[i]->worldPos() != drag_initial_positions_[i]) {
            any_moved = true;
            break;
        }
    }
    if (!any_moved) {
        model_.discard_last_checkpoint();
        return;
    }
    for (auto* widget : nodes) {
        ui::InternedId node_iid = interner_.intern(widget->id());
        if (!node_iid.empty()) {
            execute(model_, interner_, cmd_move_node(node_iid, widget->worldPos().x, widget->worldPos().y));
        }
    }
    debug_validate_command_boundary(model_, interner_, arena_);
}

void CanvasInput::commit_drag_routing_point() {
    const bp2::Blueprint::Wire* bp2_wire = model_.current().find_wire(rp_wire_id_);
    if (!bp2_wire) {
        model_.discard_last_checkpoint();
        return;
    }
    Pt final_pos = rp_point_ ? rp_point_->worldPos() : Pt(0, 0);

    // Build new routing points list
    std::vector<std::pair<float,float>> new_points;
    new_points.reserve(rp_initial_points_.size());
    for (const auto& pt : rp_initial_points_)
        new_points.push_back({pt.x, pt.y});
    if (rp_index_ < new_points.size()) {
        new_points[rp_index_] = {final_pos.x, final_pos.y};
    }

    // Check if changed
    bool changed = false;
    if (new_points.size() == rp_initial_points_.size()) {
        for (size_t i = 0; i < new_points.size(); ++i) {
            if (new_points[i].first  != rp_initial_points_[i].x ||
                new_points[i].second != rp_initial_points_[i].y) {
                changed = true;
                break;
            }
        }
    } else {
        changed = true;
    }

    if (!changed || rp_wire_id_.empty()) {
        model_.discard_last_checkpoint();
    } else {
        execute(model_, interner_, cmd_set_routing_points(rp_wire_id_, std::move(new_points)));
        debug_validate_command_boundary(model_, interner_, arena_);
    }
}

void CanvasInput::commit_resize_node() {
    auto* resize_widget = resolve_node(resize_widget_id_);
    if (!resize_widget) {
        model_.discard_last_checkpoint();
        return;
    }
    Pt new_pos = resize_widget->worldPos();
    Pt new_size = resize_widget->size();
    ui::InternedId node_iid = resize_widget_id_;
    if ((new_pos == resize_original_pos_ && new_size == resize_original_size_) || node_iid.empty()) {
        model_.discard_last_checkpoint();
    } else {
        execute(model_, interner_, cmd_resize_node(node_iid, new_pos.x, new_pos.y, new_size.x, new_size.y));
        debug_validate_command_boundary(model_, interner_, arena_);
    }
}

// ============================================================================
// on_mouse_up
// ============================================================================

InputResult CanvasInput::on_mouse_up(MouseButton btn, Pt screen_pos, Pt canvas_min) {
    InputResult result;

    if (btn == MouseButton::Left) {
        switch (state_) {
            case InputState::ReconnectingWire:
                result = finish_wire_reconnection(screen_pos, canvas_min);
                break;

            case InputState::CreatingWire:
                result = finish_wire_creation(screen_pos, canvas_min);
                break;

            case InputState::MarqueeSelect:
                finish_marquee();
                break;

            case InputState::DraggingNode:
                commit_drag_node();
                break;

            case InputState::DraggingRoutingPoint:
                commit_drag_routing_point();
                break;

            case InputState::ResizingNode:
                commit_resize_node();
                break;

            default:
                break;
        }
        leave_state();
    }
    return result;
}

// ============================================================================
// on_scroll
// ============================================================================

InputResult CanvasInput::on_scroll(float delta, Pt screen_pos, Pt canvas_min) {
    viewport_.zoom_at(delta, screen_pos, canvas_min);
    return {};
}

// ============================================================================
// on_double_click
// ============================================================================

InputResult CanvasInput::on_double_click(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);

    auto hit = visual::hit_test(scene_, world);

    // 1. Routing-point removal (editing operation — skip in read-only)
    if (!read_only) {
        if (auto* hrp = std::get_if<visual::HitRoutingPoint>(&hit)) {
            ui::InternedId wire_iid = interner_.intern(hrp->wire->id());
            const bp2::Blueprint::Wire* bp2_wire = model_.current().find_wire(wire_iid);
            if (bp2_wire && hrp->index < bp2_wire->routing_points.size()) {
                // Build new routing points with the target point removed
                auto new_points = bp2_wire->routing_points;
                new_points.erase(new_points.begin() + static_cast<long>(hrp->index));

                if (!wire_iid.empty()) {
                    snapshot_and_execute(cmd_set_routing_points(wire_iid, std::move(new_points)));
                    // Update visual: remove routing point widget
                    if (hrp->wire) {
                        hrp->wire->removeRoutingPoint(hrp->index);
                    }
                }
            }
            return result;
        }
    }

    // 2. Node hit → open sub-window for Blueprint nodes (always allowed)
    if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
        std::string node_id(hn->widget->id());
        ui::InternedId node_iid = interner_.lookup(node_id);
        const bp2::Blueprint::Node* node = node_iid.empty() ? nullptr
                                                            : model_.current().find_node(node_iid);
        if (node && node->expandable) {
            result.open_sub_window = node_id;
            return result;
        }
    }

    // 3. Wire hit → add routing point (editing operation — skip in read-only)
    if (!read_only) {
        if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
            ui::InternedId wire_iid = interner_.intern(hw->wire->id());
            const bp2::Blueprint::Wire* bp2_wire = model_.current().find_wire(wire_iid);
            if (bp2_wire) {
                // Build new routing points with the new point inserted
                auto new_points = bp2_wire->routing_points;
                Pt snapped = editor_math::snap_to_grid(world, viewport_.grid_step);
                size_t insert_idx = hw->segment;
                new_points.insert(
                    new_points.begin() + static_cast<long>(insert_idx),
                    {snapped.x, snapped.y});

                if (!wire_iid.empty()) {
                    snapshot_and_execute(cmd_set_routing_points(wire_iid, std::move(new_points)));
                    // Update visual: add routing point widget
                    if (hw->wire) {
                        hw->wire->addRoutingPoint(snapped, insert_idx);
                    }
                }
            }
        }
    }

    return result;
}

// ============================================================================
// on_key
// ============================================================================

InputResult CanvasInput::on_key(Key key) {
    InputResult result;

    if (read_only) {
        // Read-only: only Escape (clear selection) is allowed
        if (key == Key::Escape) clear_selection();
        return result;
    }

    switch (key) {
        case Key::Escape:
            // If a gesture is in-flight, cancel it and revert any partial mutations.
            if (state_ != InputState::Idle && state_ != InputState::Panning) {
                bool needs_rebuild =
                    state_ == InputState::DraggingNode ||
                    state_ == InputState::DraggingRoutingPoint ||
                    state_ == InputState::ResizingNode;
                cancel_gesture();
                if (needs_rebuild) {
                    visual::mutations::rebuild(scene_, model_.current(), interner_, arena_, group_id_);
                }
            }
            clear_selection();
            break;

        case Key::Delete:
        case Key::Backspace: {
            if (selected_node_ids_.empty()) break;

            // Checkpoint before deletion
            model_.push_checkpoint();
            for (const auto& nid : selected_node_ids_) {
                if (!nid.empty()) {
                    std::vector<ui::InternedId> connected_wires;
                    connected_wires.reserve(model_.current().wires().size());
                    for (const auto& w : model_.current().wires()) {
                        auto [src_node, _src_port] = path_to_node_port(w.source, arena_);
                        auto [tgt_node, _tgt_port] = path_to_node_port(w.target, arena_);
                        if (src_node == nid || tgt_node == nid) {
                            connected_wires.push_back(w.id);
                        }
                    }
                    execute(model_, interner_, cmd_remove_node(nid, std::move(connected_wires)));
                }
            }
            debug_validate_command_boundary(model_, interner_, arena_);
            // Nullify transient widget pointer before rebuild destroys
            // the scene graph — hovered_routing_point_ may reference a
            // widget that is about to be freed.
            hovered_routing_point_ = nullptr;
            visual::mutations::rebuild(scene_, model_.current(), interner_, arena_, group_id_);
            clear_selection();
            result.rebuild_simulation = true;
            break;
        }

        case Key::R:
            // TODO: Wire auto-routing with new system
            break;

        case Key::Z:
        case Key::Y:
            // Undo/Redo shortcuts are handled at the app level
            // (EditorApp::update) using Document::performUndo/Redo,
            // which correctly rebuilds all windows.
            break;

        case Key::RightBracket: {
            viewport_.grid_step_up();
            float new_step = viewport_.grid_step;
            snapshot_and_execute(cmd_set_grid_step(new_step));
            break;
        }

        case Key::LeftBracket: {
            viewport_.grid_step_down();
            float new_step = viewport_.grid_step;
            snapshot_and_execute(cmd_set_grid_step(new_step));
            break;
        }

        default:
            break;
    }
    return result;
}

// ============================================================================
// Wire creation / reconnection finishers
// ============================================================================

InputResult CanvasInput::finish_wire_creation(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    auto port_hit = visual::hit_test_ports(scene_, world);

    if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
        if (!wire_start_port_ || ph->port == wire_start_port_) return result;

        visual::Port* start_port = wire_start_port_;
        visual::Port* end_port = ph->port;

        // Check side pairing
        bool compatible = visual::Port::areSidesCompatible(start_port->side(), end_port->side());
        if (!compatible) return result;

        // Check type pairing
        if (!visual::Port::areTypesCompatible(start_port->type(), end_port->type())) {
            return result;
        }

        // Find owning node IDs by walking up the widget tree
        std::string_view start_node_sv = start_port->rootAncestorId();
        std::string_view end_node_sv = end_port->rootAncestorId();

        if (start_node_sv.empty() || end_node_sv.empty()) return result;

        // Check not same port
        if (start_node_sv == end_node_sv &&
            start_port->name() == end_port->name()) return result;

        ui::InternedId start_node_iid = interner_.intern(start_node_sv);
        ui::InternedId start_port_iid = interner_.intern(start_port->name());
        ui::InternedId end_node_iid   = interner_.intern(end_node_sv);
        ui::InternedId end_port_iid   = interner_.intern(end_port->name());

        // Bus visual alias ports are wire-id based; serialize canonical bus port "v".
        if (is_bus_node(model_, start_node_iid) && start_port_iid != interner_.intern("v")) {
            start_port_iid = interner_.intern("v");
        }
        if (is_bus_node(model_, end_node_iid) && end_port_iid != interner_.intern("v")) {
            end_port_iid = interner_.intern("v");
        }

        auto can_drive = [](PortSide s) {
            return s == PortSide::Output || s == PortSide::InOut;
        };
        auto can_receive = [](PortSide s) {
            return s == PortSide::Input || s == PortSide::InOut;
        };

        // Normalize orientation so source can drive and target can receive.
        // This keeps connection semantics valid when the drag starts from an
        // input or inout endpoint and is released on an output endpoint.
        const bool forward_ok = can_drive(start_port->side()) && can_receive(end_port->side());
        const bool reverse_ok = can_drive(end_port->side()) && can_receive(start_port->side());
        if (!forward_ok && !reverse_ok) {
            return result;
        }
        if (!forward_ok && reverse_ok) {
            std::swap(start_node_iid, end_node_iid);
            std::swap(start_port_iid, end_port_iid);
        }

        // Allocate wire ID
        std::string wire_id_str = model_.allocate_wire_id();
        ui::InternedId wire_iid = interner_.intern(wire_id_str);

        // Build bp2::Blueprint::Wire with Path source/target
        bp2::Path node_path = arena_.make_node(arena_.root(), start_node_iid);
        bp2::Path source    = arena_.make_port(node_path, start_port_iid);
        bp2::Path tgt_node  = arena_.make_node(arena_.root(), end_node_iid);
        bp2::Path target    = arena_.make_port(tgt_node, end_port_iid);

        bp2::Blueprint::Wire w;
        w.id     = wire_iid;
        w.source = source;
        w.target = target;

        bool added = model_.add_wire(std::move(w));
        if (added) {
            debug_validate_command_boundary(model_, interner_, arena_);
            // Rebuild visual scene to show the new wire
            visual::mutations::rebuild(scene_, model_.current(), interner_, arena_, group_id_);
            result.rebuild_simulation = true;
        }
    }
    return result;
}

InputResult CanvasInput::finish_wire_reconnection(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    auto port_hit = visual::hit_test_ports(scene_, world);

    bool reconnected = false;

    const auto& wires = model_.current().wires();
    if (reconnect_wire_idx_ >= wires.size()) return result;

    const bp2::Blueprint::Wire& wire = wires[reconnect_wire_idx_];
    auto [wire_src_node, wire_src_port] = path_to_node_port(wire.source, arena_);
    auto [wire_tgt_node, wire_tgt_port] = path_to_node_port(wire.target, arena_);

    if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
        std::string_view port_node_sv = ph->port->rootAncestorId();
        ui::InternedId port_node_iid  = interner_.intern(port_node_sv);
        ui::InternedId hit_port_iid   = interner_.intern(ph->port->name());

        // Determine which end we are detaching
        ui::InternedId detached_node = reconnect_detach_start_ ? wire_src_node : wire_tgt_node;
        ui::InternedId detached_port = reconnect_detach_start_ ? wire_src_port : wire_tgt_port;
        ui::InternedId fixed_node    = reconnect_detach_start_ ? wire_tgt_node : wire_src_node;
        ui::InternedId fixed_port    = reconnect_detach_start_ ? wire_tgt_port : wire_src_port;

        // Check if dropped back on same port
        bool same_as_original = (port_node_iid == detached_node && hit_port_iid == detached_port);
        if (same_as_original) return result;

        // Check not same as fixed end
        bool same_as_fixed = (port_node_iid == fixed_node && hit_port_iid == fixed_port);
        bool compatible = !same_as_fixed &&
            visual::Port::areSidesCompatible(ph->port->side(), reconnect_fixed_side_);
        if (compatible && !visual::Port::areTypesCompatible(ph->port->type(), reconnect_fixed_type_)) {
            compatible = false;
        }

        if (is_bus_node(model_, port_node_iid) && is_wire_alias_port_name(ph->port->name())) {
            size_t target_wire_idx = find_wire_index(hit_port_iid);
            if (target_wire_idx != SIZE_MAX && target_wire_idx < wires.size()) {
                if (target_wire_idx != reconnect_wire_idx_) {
                    auto reordered = wires;
                    std::swap(reordered[reconnect_wire_idx_], reordered[target_wire_idx]);

                    bp2::Blueprint updated_bp = model_.current();
                    for (const auto& old_wire : model_.current().wires()) {
                        updated_bp = updated_bp.without_wire(old_wire.id);
                    }
                    for (const auto& new_wire : reordered) {
                        updated_bp = updated_bp.with_wire(new_wire);
                    }

                    model_.push_checkpoint();
                    model_.replace_current(std::move(updated_bp));
                    debug_validate_command_boundary(model_, interner_, arena_);
                    visual::mutations::rebuild(scene_, model_.current(), interner_, arena_, group_id_);
                    result.rebuild_simulation = true;
                    reconnected = true;
                } else {
                    reconnected = true;
                }
            }
        } else if (compatible) {
            // Build updated wire with new endpoint
            ui::InternedId new_node_iid = interner_.intern(port_node_sv);
            ui::InternedId new_port_iid = interner_.intern(ph->port->name());

            // Bus visual alias ports are wire-id based; serialize canonical bus port "v".
            if (is_bus_node(model_, new_node_iid) && new_port_iid != interner_.intern("v")) {
                new_port_iid = interner_.intern("v");
            }

            bp2::Path new_node_path = arena_.make_node(arena_.root(), new_node_iid);
            bp2::Path new_port_path = arena_.make_port(new_node_path, new_port_iid);

            bool updated_ok = model_.update_wire(wire.id, [&](bp2::Blueprint::Wire& wr) {
                if (reconnect_detach_start_) {
                    wr.source = new_port_path;
                } else {
                    wr.target = new_port_path;
                }
            });

            if (updated_ok) {
                debug_validate_command_boundary(model_, interner_, arena_);
                visual::mutations::rebuild(scene_, model_.current(), interner_, arena_, group_id_);
                result.rebuild_simulation = true;
                reconnected = true;
            }
        }
    }

    if (!reconnected && reconnect_wire_idx_ < model_.current().wires().size()) {
        // Wire dropped on empty space → remove it
        if (model_.remove_wire(wire.id)) {
            visual::mutations::rebuild(scene_, model_.current(), interner_, arena_, group_id_);
            result.rebuild_simulation = true;
        }
    }

    return result;
}

// ============================================================================
// Marquee finisher
// ============================================================================

void CanvasInput::finish_marquee() {
    float min_x = std::min(marquee_start_.x, marquee_end_.x);
    float max_x = std::max(marquee_start_.x, marquee_end_.x);
    float min_y = std::min(marquee_start_.y, marquee_end_.y);
    float max_y = std::max(marquee_start_.y, marquee_end_.y);

    for (const auto& root : scene_.roots()) {
        auto* vroot = static_cast<visual::Widget*>(root.get());
        // Only select node widgets (skip wires)
        if (vroot->renderLayer() == visual::RenderLayer::Wire) continue;
        // Only select nodes that have an ID (i.e., are actual node widgets)
        if (vroot->id().empty()) continue;

        // Verify this node belongs to our group
        ui::InternedId node_iid = interner_.lookup(std::string_view(vroot->id()));
        const bp2::Blueprint::Node* node = node_iid.empty() ? nullptr
                                                            : model_.current().find_node(node_iid);
        if (!node || node->group_id != group_id_) continue;

        Pt pos = vroot->worldPos();
        Pt sz = vroot->size();
        float cx = pos.x + sz.x / 2;
        float cy = pos.y + sz.y / 2;
        if (cx >= min_x && cx <= max_x && cy >= min_y && cy <= max_y)
            add_node_selection(vroot);
    }
}

// ============================================================================
// Utility helpers
// ============================================================================

size_t CanvasInput::find_wire_index(ui::InternedId wire_id) const {
    if (wire_id.empty()) return SIZE_MAX;
    const auto& wires = model_.current().wires();
    for (size_t i = 0; i < wires.size(); ++i) {
        if (wires[i].id == wire_id) return i;
    }
    return SIZE_MAX;
}

size_t CanvasInput::find_node_index(ui::InternedId node_id) const {
    if (node_id.empty()) return SIZE_MAX;
    const auto& nodes = model_.current().nodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].id == node_id) return i;
    }
    return SIZE_MAX;
}

std::optional<CanvasInput::WirePortMatch> CanvasInput::find_wire_on_port(visual::Port* port) const {
    if (!port) return std::nullopt;

    std::string_view port_node_sv = port->rootAncestorId();
    std::string_view port_name_sv = port->name();

    ui::InternedId port_node_iid = interner_.intern(port_node_sv);
    ui::InternedId port_name_iid = interner_.intern(port_name_sv);

    // Bus ports are special: alias ports are named by wire ID.
    // Clicking base bus port "v" should start wire creation, not reconnect
    // an arbitrary first wire.
    if (is_bus_node(model_, port_node_iid)) {
        if (port_name_sv == "v") {
            return std::nullopt;
        }

        if (is_wire_alias_port_name(port_name_sv)) {
            size_t wi = find_wire_index(port_name_iid);
            if (wi == SIZE_MAX) return std::nullopt;
            const bp2::Blueprint::Wire& w = model_.current().wires()[wi];

            auto build_result = [&](bool detach_start) -> WirePortMatch {
                Pt anchor_pos;
                PortSide fixed_side;
                PortType fixed_type = PortType::Any;
                if (detach_start) {
                    fixed_side = PortSide::Input;
                    auto [tgt_node, tgt_port] = path_to_node_port(w.target, arena_);
                    fixed_type = resolve_port_type_from_model(model_, tgt_node, tgt_port);
                    if (!w.routing_points.empty()) {
                        anchor_pos = Pt(w.routing_points.front().first, w.routing_points.front().second);
                    } else {
                        auto* end_widget = scene_.find(interner_.resolve(tgt_node));
                        if (end_widget) {
                            auto* end_port = end_widget->portByName(interner_.resolve(tgt_port), interner_.resolve(w.id));
                            if (end_port) {
                                anchor_pos = end_port->worldPos() + Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
                                fixed_type = end_port->type();
                            }
                        }
                    }
                } else {
                    fixed_side = PortSide::Output;
                    auto [src_node, src_port] = path_to_node_port(w.source, arena_);
                    fixed_type = resolve_port_type_from_model(model_, src_node, src_port);
                    if (!w.routing_points.empty()) {
                        anchor_pos = Pt(w.routing_points.back().first, w.routing_points.back().second);
                    } else {
                        auto* start_widget = scene_.find(interner_.resolve(src_node));
                        if (start_widget) {
                            auto* start_port = start_widget->portByName(interner_.resolve(src_port), interner_.resolve(w.id));
                            if (start_port) {
                                anchor_pos = start_port->worldPos() + Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
                                fixed_type = start_port->type();
                            }
                        }
                    }
                }
                return WirePortMatch{wi, detach_start, anchor_pos, fixed_side, fixed_type};
            };

            auto [src_node, _src_port] = path_to_node_port(w.source, arena_);
            auto [tgt_node, _tgt_port] = path_to_node_port(w.target, arena_);
            if (src_node == port_node_iid) return build_result(true);
            if (tgt_node == port_node_iid) return build_result(false);
        }
        return std::nullopt;
    }

    // Helper: build WirePortMatch for a given wire index and side.
    auto build_result = [&](size_t wi, bool detach_start) -> WirePortMatch {
        const auto& w = model_.current().wires()[wi];
        Pt anchor_pos;
        PortSide fixed_side;
        PortType fixed_type = PortType::Any;
        if (detach_start) {
            fixed_side = PortSide::Input;  // fixed end is target
            auto [tgt_node, tgt_port] = path_to_node_port(w.target, arena_);
            fixed_type = resolve_port_type_from_model(model_, tgt_node, tgt_port);
            if (!w.routing_points.empty()) {
                anchor_pos = Pt(w.routing_points.front().first, w.routing_points.front().second);
            } else {
                auto* end_widget = scene_.find(interner_.resolve(tgt_node));
                if (end_widget) {
                    auto* end_port = end_widget->portByName(interner_.resolve(tgt_port),
                                                             interner_.resolve(w.id));
                    if (end_port) {
                        anchor_pos = end_port->worldPos() + Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
                        fixed_type = end_port->type();
                    }
                }
            }
        } else {
            fixed_side = PortSide::Output;  // fixed end is source
            auto [src_node, src_port] = path_to_node_port(w.source, arena_);
            fixed_type = resolve_port_type_from_model(model_, src_node, src_port);
            if (!w.routing_points.empty()) {
                anchor_pos = Pt(w.routing_points.back().first, w.routing_points.back().second);
            } else {
                auto* start_widget = scene_.find(interner_.resolve(src_node));
                if (start_widget) {
                    auto* start_port = start_widget->portByName(interner_.resolve(src_port),
                                                                  interner_.resolve(w.id));
                    if (start_port) {
                        anchor_pos = start_port->worldPos() + Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
                        fixed_type = start_port->type();
                    }
                }
            }
        }
        return WirePortMatch{wi, detach_start, anchor_pos, fixed_side, fixed_type};
    };

    // Scan all wires looking for one that connects to (port_node_iid, port_name_iid)
    const auto& wires = model_.current().wires();
    for (size_t wi = 0; wi < wires.size(); ++wi) {
        const bp2::Blueprint::Wire& w = wires[wi];
        auto [src_node, src_port] = path_to_node_port(w.source, arena_);
        auto [tgt_node, tgt_port] = path_to_node_port(w.target, arena_);

        if (src_node == port_node_iid && src_port == port_name_iid) {
            return build_result(wi, true);   // detach start
        }
        if (tgt_node == port_node_iid && tgt_port == port_name_iid) {
            return build_result(wi, false);  // detach end
        }
    }

    return std::nullopt;
}
