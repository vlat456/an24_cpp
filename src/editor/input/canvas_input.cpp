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

using namespace canvas_input_impl;

// ============================================================================
// Construction
// ============================================================================

CanvasInput::CanvasInput(visual::Scene& scene, Viewport& viewport,
                         bp2::EditorModel& model, ui::StringInterner& interner,
                         bp2::PathArena& arena, const std::string& group_id,
                         const TypeRegistry* parser_registry)
    : scene_(scene), viewport_(viewport), model_(model),
      interner_(interner), arena_(arena),
      parser_registry_(parser_registry),
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
    debug_validate_command_boundary(model_, interner_, arena_, parser_registry_);
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
        state_ == InputState::DraggingSlider) {
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
}

void CanvasInput::enter_drag_routing_point(visual::Wire* wire, visual::RoutingPoint* rp, size_t rp_idx) {
    state_ = InputState::DraggingRoutingPoint;
    auto wire_iid = interner_.intern(wire->id());
    selected_wire_id_ = wire_iid;
    rp_wire_id_ = wire_iid;
    rp_point_ = rp;
    rp_index_ = rp_idx;
    drag_anchor_ = rp->worldPos();

    const bp2::Blueprint::Wire* bp2_wire = model_.current().find_wire(rp_wire_id_);
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

void CanvasInput::enter_drag_slider(visual::Widget* node_widget, Pt slider_world_pos, float slider_width) {
    state_ = InputState::DraggingSlider;
    slider_node_id_ = interner_.intern(node_widget->id());
    slider_widget_world_pos_ = slider_world_pos;
    slider_widget_width_ = slider_width;
}

void CanvasInput::enter_drag_knob(visual::Widget* node_widget, Pt world_pos) {
    state_ = InputState::DraggingKnob;
    knob_node_id_ = interner_.intern(node_widget->id());
    knob_drag_start_x_ = world_pos.x;

     const bp2::Blueprint::Node* node = model_.current().find_node(knob_node_id_);
     if (node) {
         knob_drag_start_pos_ = static_cast<int>(node->view.content_value);
         knob_num_positions_ = static_cast<int>(node->view.content_max);
         if (knob_num_positions_ < 2) knob_num_positions_ = 2;
     } else {
        knob_drag_start_pos_ = 0;
        knob_num_positions_ = 2;
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
}

bool CanvasInput::try_handle_node_interaction(visual::Widget* widget, Pt world, InputResult& result) {
    auto* node_widget = dynamic_cast<visual::NodeWidget*>(widget);
    if (!node_widget) {
        return false;
    }

    const auto interaction = node_widget->query_interaction(world);
    if (!interaction.has_value()) {
        return false;
    }

    const std::string node_id(widget->id());
    switch (interaction->type) {
        case visual::NodeInteractionType::Slider: {
            const Bounds cb = node_widget->contentBounds();
            const Pt nw_pos = node_widget->worldPos();
            const Pt slider_wpos(nw_pos.x + cb.x, nw_pos.y + cb.y);
            enter_drag_slider(widget, slider_wpos, cb.w);

            ui::InternedId nid_iid = interner_.lookup(node_id);
            const bp2::Blueprint::Node* node = nid_iid.empty() ? nullptr
                                                               : model_.current().find_node(nid_iid);
            if (node) {
                float pad = visual::SliderWidget::HANDLE_RADIUS;
                float track_w = cb.w - 2.0f * pad;
                 float t = (track_w > 0.0f)
                     ? std::clamp((interaction->content_local_x - pad) / track_w, 0.0f, 1.0f)
                     : 0.0f;
                 float val = node->view.content_min + t * (node->view.content_max - node->view.content_min);
                 result.slider_node_id = node_id;
                 result.slider_value = val;
            }
            return true;
        }
        case visual::NodeInteractionType::Knob:
            enter_drag_knob(widget, world);
            result.knob_node_id = node_id;
            result.knob_position = knob_drag_start_pos_;
            return true;
        case visual::NodeInteractionType::Toggle:
            result.toggle_switch_node_id = node_id;
            return true;
    }

    return false;
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
        if (nodes[i].semantic.id == node_id) return i;
    }
    return SIZE_MAX;
}
