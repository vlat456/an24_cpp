#include "input/canvas_input.h"
#include "input/canvas_input_internal.h"
#include "visual/scene.h"
#include "visual/scene_hittest.h"
#include "visual/scene_mutations.h"
#include "visual/widget.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "visual/port/visual_port.h"
#include "visual/node/group_node_widget.h"
#include "visual/node/visual_node.h"
#include "visual/node/ref_node_widget.h"
#include "visual/snap.h"
#include "viewport/viewport.h"
#include "commands/commands.h"
#include "visual/persist.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "debug.h"
#include <imgui.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <unordered_set>

using namespace canvas_input_impl;

// ============================================================================
// Construction
// ============================================================================

CanvasInput::CanvasInput(visual::Scene& scene, Viewport& viewport,
                         EditingHost& host, ui::StringInterner& interner,
                         bp2::PathArena& arena, const std::string& scope_id,
                         const TypeRegistry* parser_registry)
    : scene_(scene), viewport_(viewport), host_(host),
      interner_(interner), arena_(arena),
      parser_registry_(parser_registry),
      group_iid_(interner.intern(scope_id)),
      scope_id_(interner.resolve(group_iid_))
{
}

// ============================================================================
// Command execution (checkpoint-based)
// ============================================================================

void CanvasInput::snapshot_and_execute(Command cmd) {
    std::visit([&](auto c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, CmdSetRoutingPoints>) {
            host_.update_wire(c.wire_id, [&](bp2::Blueprint::Wire& w) {
                w.routing_points = std::move(c.points);
            });
        } else {
            throw std::logic_error("CanvasInput::snapshot_and_execute received unsupported command");
        }
    }, std::move(cmd));
    debug_validate_command_boundary(host_.current_blueprint(), interner_, arena_, parser_registry_);
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
    if (state_ == InputState::DraggingNode ||
        state_ == InputState::DraggingRoutingPoint ||
        state_ == InputState::CreatingWire ||
        state_ == InputState::ReconnectingWire ||
        state_ == InputState::ResizingNode ||
        state_ == InputState::DraggingSlider ||
        state_ == InputState::DraggingKnob) {
        hovered_wire_id_ = {};
        hovered_routing_point_ = nullptr;
        return;
    }

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
    if (!ctrl && !is_node_selected(widget)) clear_selection();
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
}

void CanvasInput::enter_drag_routing_point(visual::Wire* wire, visual::RoutingPoint* rp, size_t rp_idx) {
    state_ = InputState::DraggingRoutingPoint;
    auto wire_iid = interner_.intern(wire->id());
    selected_wire_id_ = wire_iid;
    rp_wire_id_ = wire_iid;
    rp_point_ = rp;
    rp_index_ = rp_idx;
    drag_anchor_ = rp->worldPos();

    const bp2::Blueprint::Wire* bp2_wire = host_.find_wire(rp_wire_id_);
    if (bp2_wire) {
        rp_initial_points_.clear();
        rp_initial_points_.reserve(bp2_wire->routing_points.size());
        for (const auto& [rx, ry] : bp2_wire->routing_points)
            rp_initial_points_.push_back(Pt(rx, ry));
    } else {
        rp_initial_points_.clear();
    }
}

void CanvasInput::enter_resize_node(visual::Widget* widget, ResizeCorner corner) {
    state_ = InputState::ResizingNode;
    clear_selection();
    add_node_selection(widget);
    resize_widget_id_ = interner_.intern(widget->id());
    resize_corner_ = corner;
    resize_original_pos_ = widget->worldPos();
    resize_original_size_ = widget->size();
    drag_anchor_ = Pt(0, 0);
}

void CanvasInput::enter_create_wire(visual::Port* port, Pt port_pos) {
    state_ = InputState::CreatingWire;
    wire_start_port_ = port;
    wire_start_pos_ = port_pos;
}

void CanvasInput::enter_reconnect_wire(size_t wire_idx, bool detach_start,
                                       Pt anchor_pos, bp2::PortSide fixed_side, PortType fixed_type) {
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

void CanvasInput::setup_semantic_interaction_state(visual::Widget* node_widget, const visual::InteractionTarget& target,
                                                    const editor::presentation::Rect& interaction_bounds, Pt world_pos) {
    ui::InternedId node_id = interner_.intern(node_widget->id());
    const bp2::Blueprint::Node* node = host_.find_node(node_id);
    if (!node) return;

    semantic_canvas_controller_.set_snapshot(
        canvas_input_impl::build_interaction_snapshot(node_id, interaction_bounds, target));

    switch (target.role) {
        case visual::InteractionRole::ContinuousScalar: {
            state_ = InputState::DraggingSlider;
            auto* visual_node = dynamic_cast<visual::NodeWidget*>(node_widget);
            float origin_world_x = visual_node
                ? visual_node->worldPos().x + visual_node->contentBounds().x
                : node_widget->worldPos().x;
            semantic_canvas_controller_.set_active_scalar_mapping({
                origin_world_x,
                target.primary_min,
                target.primary_max,
                node->view.content_min,
                node->view.content_max,
            });
            break;
        }
        case visual::InteractionRole::DiscreteSelector: {
            state_ = InputState::DraggingKnob;
            int start_pos = static_cast<int>(node->view.content_value);
            int num_positions = target.steps;
            if (num_positions < 2) num_positions = 2;
            semantic_canvas_controller_.set_active_discrete_mapping({
                world_pos.x,
                start_pos,
                num_positions,
                30.0f,
            });
            break;
        }
        case visual::InteractionRole::Toggle:
            break;
    }
}

void CanvasInput::leave_state() {
    state_ = InputState::Idle;
    drag_offsets_.clear();
    drag_initial_positions_.clear();
    rp_initial_points_.clear();
    wire_start_port_ = nullptr;
    rp_point_ = nullptr;
    hovered_routing_point_ = nullptr;
    semantic_canvas_controller_.reset();
    semantic_canvas_controller_.clear_active_mapping();
}

editor::presentation::SemanticCanvasControllerResult CanvasInput::configure_and_dispatch_semantic_interaction(
    visual::Widget* node_widget, const visual::InteractionTarget& target,
    const editor::presentation::Rect& interaction_bounds, Pt world) {
    setup_semantic_interaction_state(node_widget, target, interaction_bounds, world);

    switch (target.role) {
        case visual::InteractionRole::ContinuousScalar:
        case visual::InteractionRole::DiscreteSelector:
            return semantic_canvas_controller_.on_pointer_drag(world);

        case visual::InteractionRole::Toggle:
            return semantic_canvas_controller_.on_pointer_press(world);
    }

    return editor::presentation::SemanticCanvasControllerResult{};
}

bool CanvasInput::publish_semantic_control_result(
    const editor::presentation::SemanticCanvasControllerResult& semantic,
    InputResult& result) const {
    switch (semantic.control_event.kind) {
        case editor::presentation::SemanticControlEventKind::Toggle:
            result.toggle_switch_node_id = std::string(interner_.resolve(semantic.control_event.node_id));
            return true;
        case editor::presentation::SemanticControlEventKind::SetScalar:
            result.slider_node_id = std::string(interner_.resolve(semantic.control_event.node_id));
            result.slider_value = semantic.control_event.scalar_value;
            return true;
        case editor::presentation::SemanticControlEventKind::SetDiscrete:
            result.knob_node_id = std::string(interner_.resolve(semantic.control_event.node_id));
            result.knob_position = semantic.control_event.discrete_value;
            return true;
        case editor::presentation::SemanticControlEventKind::None:
            return false;
    }

    return false;
}

bool CanvasInput::state_uses_semantic_control_session() const {
    return state_ == InputState::DraggingSlider || state_ == InputState::DraggingKnob;
}

bool CanvasInput::handle_resolved_interaction(visual::Widget* widget, const visual::InteractionTarget& target, Pt world, InputResult& result) {
    auto* node_widget = dynamic_cast<visual::NodeWidget*>(widget);
    if (!node_widget) {
        return false;
    }

    const std::string node_id(widget->id());
    ui::InternedId nid_iid = interner_.intern(node_id);
    const bp2::Blueprint::Node* node = host_.find_node(nid_iid);
    if (!node) {
        return false;
    }

    const Bounds content_bounds = node_widget->contentBounds();
    const Pt node_world = node_widget->worldPos();
    const editor::presentation::Rect interaction_bounds{
        node_world.x + content_bounds.x + target.geometry.origin.x,
        node_world.y + content_bounds.y + target.geometry.origin.y,
        target.geometry.size.x,
        target.geometry.size.y,
    };

    semantic_canvas_controller_.reset();
    editor::presentation::SemanticCanvasControllerResult semantic =
        configure_and_dispatch_semantic_interaction(widget, target, interaction_bounds, world);
    return publish_semantic_control_result(semantic, result);
}


void CanvasInput::clear_selection_and_enter_panning() {
    clear_selection();
    enter_panning();
}

void CanvasInput::advance_world_cursor(Pt world_delta) {
    last_world_pos_ = last_world_pos_ + world_delta;
}

void CanvasInput::snapshot_wire_routing_points(ui::InternedId wire_id,
                                               std::vector<std::pair<float, float>> new_points) {
    if (wire_id.empty()) {
        return;
    }
    snapshot_and_execute(cmd_set_routing_points(wire_id, std::move(new_points)));
}

void CanvasInput::cancel_gesture() {
    if (state_uses_semantic_control_session()) {
        semantic_canvas_controller_.cancel();
        state_ = InputState::Idle;
        drag_offsets_.clear();
        drag_initial_positions_.clear();
        rp_initial_points_.clear();
        wire_start_port_ = nullptr;
        rp_point_ = nullptr;
        hovered_routing_point_ = nullptr;
        semantic_canvas_controller_.clear_active_mapping();
        return;
    }
    leave_state();
}

// ============================================================================
// on_scroll
// ============================================================================

InputResult CanvasInput::on_scroll(float delta, Pt screen_pos, Pt canvas_min) {
    viewport_.zoom_at(delta, screen_pos, canvas_min);
    return {};
}

// ============================================================================
// Utility helpers
// ============================================================================

size_t CanvasInput::find_wire_index(ui::InternedId wire_id) const {
    if (wire_id.empty()) return SIZE_MAX;
    const auto& wires = host_.wires();
    for (size_t i = 0; i < wires.size(); ++i) {
        if (wires[i].id == wire_id) return i;
    }
    return SIZE_MAX;
}

size_t CanvasInput::find_node_index(ui::InternedId node_id) const {
    if (node_id.empty()) return SIZE_MAX;
    const auto& nodes = host_.nodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].semantic.id == node_id) return i;
    }
    return SIZE_MAX;
}
