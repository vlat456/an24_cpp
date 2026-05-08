#include "input/canvas_input.h"
#include "visual/scene.h"
#include "visual/scene_mutations.h"
#include "visual/widget.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "visual/port/visual_port.h"
#include "visual/node/group_node_widget.h"
#include "visual/node/visual_node.h"
#include "visual/node/ref_node_widget.h"
#include "visual/snap.h"
#include "editor/visual/presentation/canvas_scene_snapshot.h"
#include "viewport/viewport.h"
#include "commands/commands.h"
#include "visual/presentation/semantic_scene_hittest.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"

#include <imgui.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <unordered_set>

namespace {
constexpr float DISCRETE_DRAG_PIXELS_PER_STEP = 30.0f;
}

// ============================================================================
// Construction
// ============================================================================

CanvasInput::CanvasInput(visual::Scene& scene, Viewport& viewport,
                         EditingHost* host, core::StringInterner& interner,
                         bp2::PathArena& arena, const WindowScopeId& scope_id,
                         const editor::IconFont* icon_font)
    : scene_(scene), viewport_(viewport), host_(host),
      interner_(&interner), arena_(&arena),
      scope_id_(scope_id), icon_font_(icon_font)
{
    rebuild_snapshot();
}

// ============================================================================
// Command execution (checkpoint-based)
// ============================================================================

void CanvasInput::snapshot_and_execute(Command cmd) {
    std::visit([&](auto c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, CmdSetRoutingPoints>) {
            host_->update_wire(c.wire_id, [&](bp2::Blueprint::Wire& w) {
                w.routing_points = std::move(c.points);
            });
        } else {
            throw std::logic_error("CanvasInput::snapshot_and_execute received unsupported command");
        }
    }, std::move(cmd));
    host_->debug_validate_integrity();
}

// ============================================================================
// ID → pointer resolution helpers
// ============================================================================

visual::Wire* CanvasInput::resolve_wire(core::InternedId id) const {
    if (id.empty()) return nullptr;
    auto* found = scene_.find(interner_->resolve(id));
    return (found && found->kind() == ui::WidgetKind::Wire)
           ? static_cast<visual::Wire*>(found) : nullptr;
}

visual::Widget* CanvasInput::resolve_node(core::InternedId id) const {
    if (id.empty()) return nullptr;
    return scene_.find(interner_->resolve(id));
}

// ============================================================================
// Selection helpers
// ============================================================================

void CanvasInput::clear_selection() {
    selected_node_ids_.clear();
    selected_wire_id_ = {};
}

void CanvasInput::add_node_selection(core::InternedId node_id) {
    if (node_id.empty()) return;
    if (std::find(selected_node_ids_.begin(), selected_node_ids_.end(), node_id) == selected_node_ids_.end()) {
        selected_node_ids_.push_back(node_id);
    }
}

bool CanvasInput::is_node_selected(core::InternedId node_id) const {
    if (node_id.empty()) return false;
    return std::find(selected_node_ids_.begin(), selected_node_ids_.end(), node_id) != selected_node_ids_.end();
}

std::unordered_set<std::string_view, visual::StringViewHash> CanvasInput::selected_node_id_views() const {
    std::unordered_set<std::string_view, visual::StringViewHash> result;
    result.reserve(selected_node_ids_.size());
    for (const auto& nid : selected_node_ids_) {
        result.insert(interner_->resolve(nid));
    }
    return result;
}

std::string_view CanvasInput::selected_wire_id() const {
    return interner_->resolve(selected_wire_id_);
}

std::string_view CanvasInput::hovered_wire_id() const {
    return interner_->resolve(hovered_wire_id_);
}

std::string_view CanvasInput::resize_node_id() const {
    return resize_widget_id_.empty()
        ? std::string_view{}
        : interner_->resolve(resize_widget_id_);
}

bool CanvasInput::select_node_by_id(std::string_view node_id) {
    core::InternedId const iid = interner_->intern(node_id);
    const bp2::Blueprint::Node* node = host_->find_node(iid);
    if (!node) return false;

    clear_selection();
    add_node_selection(iid);

    constexpr float DEFAULT_W = 64.0f;
    constexpr float DEFAULT_H = 32.0f;
    float const w = node->layout.width.value_or(DEFAULT_W);
    float const h = node->layout.height.value_or(DEFAULT_H);
    Pt const center(node->layout.x + w * 0.5f, node->layout.y + h * 0.5f);
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
        hovered_rp_id_ = {};
        return;
    }

    auto hit = editor::presentation::hit_test_canvas_scene(snapshot_, world_pos);
    if (auto* h = std::get_if<visual::HitWire>(&hit)) {
        hovered_wire_id_ = h->wire_id;
        hovered_rp_id_ = {};
    } else if (auto* h = std::get_if<visual::HitRoutingPoint>(&hit)) {
        hovered_wire_id_ = h->wire_id;
        hovered_rp_id_ = {h->wire_id, h->index};
    } else {
        hovered_wire_id_ = {};
        hovered_rp_id_ = {};
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

void CanvasInput::enter_drag_node(core::InternedId node_id, Pt world_pos, bool ctrl) {
    if (!ctrl && !is_node_selected(node_id)) clear_selection();
    add_node_selection(node_id);

    state_ = InputState::DraggingNode;
    drag_anchor_ = world_pos;
    drag_offsets_.clear();
    drag_initial_positions_.clear();
    drag_current_positions_.clear();
    for (const auto& selected_id : selected_node_ids_) {
        const bp2::Blueprint::Node* node = host_->find_node(selected_id);
        if (!node) continue;
        Pt const node_pos(node->layout.x, node->layout.y);
        drag_offsets_.push_back(node_pos - world_pos);
        drag_initial_positions_.push_back(node_pos);
    }
}

void CanvasInput::enter_drag_routing_point(core::InternedId wire_id, size_t rp_idx, Pt rp_world_pos) {
    state_ = InputState::DraggingRoutingPoint;
    selected_wire_id_ = wire_id;
    rp_wire_id_ = wire_id;
    rp_index_ = rp_idx;
    drag_anchor_ = rp_world_pos;
    rp_drag_pos_ = rp_world_pos;

    const bp2::Blueprint::Wire* bp2_wire = host_->find_wire(rp_wire_id_);
    if (bp2_wire) {
        rp_initial_points_.clear();
        rp_initial_points_.reserve(bp2_wire->routing_points.size());
        for (const auto& [rx, ry] : bp2_wire->routing_points)
            rp_initial_points_.push_back(Pt(rx, ry));
    } else {
        rp_initial_points_.clear();
    }
}

void CanvasInput::enter_resize_node(core::InternedId node_id, Pt world_pos, Pt size, ResizeCorner corner) {
    state_ = InputState::ResizingNode;
    clear_selection();
    add_node_selection(node_id);
    resize_widget_id_ = node_id;
    resize_corner_ = corner;
    resize_original_pos_ = world_pos;
    resize_original_size_ = size;
    resize_current_pos_ = world_pos;
    resize_current_size_ = size;
    drag_anchor_ = Pt(0, 0);
}

void CanvasInput::enter_create_wire(core::InternedId node_id, core::InternedId port_id,
                                    bp2::Direction direction, PortType type, Pt port_pos) {
    state_ = InputState::CreatingWire;
    wire_start_endpoint_ = WireStartEndpoint{node_id, port_id, direction, type};
    wire_start_pos_ = port_pos;
}

void CanvasInput::enter_reconnect_wire(size_t wire_idx, bool detach_start,
                                       Pt anchor_pos, bp2::Direction fixed_direction, PortType fixed_type) {
    state_ = InputState::ReconnectingWire;
    reconnect_wire_idx_ = wire_idx;
    reconnect_detach_start_ = detach_start;
    reconnect_anchor_pos_ = anchor_pos;
    reconnect_fixed_direction_ = fixed_direction;
    reconnect_fixed_type_ = fixed_type;
}

void CanvasInput::enter_marquee(Pt world_pos) {
    state_ = InputState::MarqueeSelect;
    marquee_start_ = world_pos;
    marquee_end_ = world_pos;
}

void CanvasInput::setup_semantic_interaction_state(const visual::HitNode& node_hit,
                                                   const CanvasInput::SemanticContentTarget& target,
                                                   Pt world_pos) {
    core::InternedId const node_id = node_hit.node_id;
    const bp2::Blueprint::Node* node = host_->find_node(node_id);
    if (!node) return;

    if (!node_hit.renders_content_from_semantic_snapshot || node_hit.content_snapshot.hit_objects.empty()) {
        return;
    }

    // Cache session geometry at interaction start
    Pt const node_world_pos = node_hit.world_pos;
    Bounds const content_bounds = node_hit.content_bounds;
    semantic_session_seed_ = SemanticSessionSeed{
        node_id,
        node_world_pos,
        content_bounds,
        node_hit.content_snapshot
    };

    semantic_canvas_controller_.set_snapshot(node_hit.content_snapshot);

    switch (target.role) {
        case CanvasInput::SemanticContentRole::ContinuousScalar: {
            state_ = InputState::DraggingSlider;
            float const origin_local_x = content_bounds.x;
            const auto spec = host_->resolve_presentation_spec(node_id);
            semantic_canvas_controller_.set_active_scalar_mapping({
                origin_local_x,
                target.primary_min,
                target.primary_max,
                spec.content_min,
                spec.content_max,
            });
            break;
        }
        case CanvasInput::SemanticContentRole::DiscreteSelector: {
            state_ = InputState::DraggingKnob;
            const auto spec = host_->resolve_presentation_spec(node_id);
            int const start_pos = static_cast<int>(spec.content_value);
            int num_positions = target.steps;
            if (num_positions < 2) num_positions = 2;
            semantic_canvas_controller_.set_active_discrete_mapping({
                world_pos.x - node_world_pos.x,
                start_pos,
                num_positions,
                DISCRETE_DRAG_PIXELS_PER_STEP,
            });
            break;
        }
        case CanvasInput::SemanticContentRole::Toggle:
            break;
    }
}

void CanvasInput::leave_state() {
    state_ = InputState::Idle;
    drag_offsets_.clear();
    drag_initial_positions_.clear();
    drag_current_positions_.clear();
    rp_initial_points_.clear();
    wire_start_endpoint_.reset();
    hovered_rp_id_ = {};
    rp_drag_pos_ = Pt(0, 0);
    semantic_canvas_controller_.reset();
    semantic_canvas_controller_.clear_active_mapping();
    semantic_session_seed_.reset();
}

editor::presentation::SemanticCanvasControllerResult CanvasInput::configure_and_dispatch_semantic_interaction(
    const visual::HitNode& node_hit, const CanvasInput::SemanticContentTarget& target, Pt world) {
    setup_semantic_interaction_state(node_hit, target, world);
    
    Pt local_point = world;
    if (semantic_session_seed_) {
        local_point = Pt(world.x - semantic_session_seed_->node_world_pos.x,
                         world.y - semantic_session_seed_->node_world_pos.y);
    }

    switch (target.role) {
        case CanvasInput::SemanticContentRole::ContinuousScalar:
        case CanvasInput::SemanticContentRole::DiscreteSelector:
            return semantic_canvas_controller_.on_pointer_drag(local_point);

        case CanvasInput::SemanticContentRole::Toggle:
            return semantic_canvas_controller_.on_pointer_press(local_point);
    }

    return editor::presentation::SemanticCanvasControllerResult{};
}

bool CanvasInput::publish_semantic_control_result(
    const editor::presentation::SemanticCanvasControllerResult& semantic,
    InputResult& result) const {
    switch (semantic.control_event.kind) {
        case editor::presentation::SemanticControlEventKind::Toggle:
            result.toggle_switch_node_id = semantic.control_event.node_id;
            return true;
        case editor::presentation::SemanticControlEventKind::SetScalar:
            result.slider_node_id = semantic.control_event.node_id;
            result.slider_value = semantic.control_event.scalar_value;
            return true;
        case editor::presentation::SemanticControlEventKind::SetDiscrete:
            result.knob_node_id = semantic.control_event.node_id;
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

bool CanvasInput::handle_resolved_interaction(const visual::HitNode& node_hit,
                                              const CanvasInput::SemanticContentTarget& target,
                                              Pt world, InputResult& result) {
    core::InternedId const node_id = node_hit.node_id;
    if (node_id.empty()) {
        return false;
    }

    if (!host_->find_node(node_id)) {
        return false;
    }

    semantic_canvas_controller_.reset();
    editor::presentation::SemanticCanvasControllerResult const semantic =
        configure_and_dispatch_semantic_interaction(node_hit, target, world);
    return publish_semantic_control_result(semantic, result);
}

void CanvasInput::clear_selection_and_enter_panning() {
    clear_selection();
    enter_panning();
}

void CanvasInput::advance_world_cursor(Pt world_delta) {
    last_world_pos_ = last_world_pos_ + world_delta;
}

void CanvasInput::snapshot_wire_routing_points(core::InternedId wire_id,
                                               std::vector<std::pair<float, float>> new_points) {
    if (wire_id.empty()) {
        return;
    }
    snapshot_and_execute(cmd_set_routing_points(wire_id, std::move(new_points)));
}

// ============================================================================
// Scene rebuild + snapshot refresh
// ============================================================================

void CanvasInput::rebuild_scene() {
    // scope_id_.path() already returns InternedId vector - use directly
    std::vector<core::InternedId> instance_path(scope_id_.path().begin(), scope_id_.path().end());
    if (const ComponentRegistry* reg = host_->type_registry()) {
        visual::mutations::rebuild(scene_, host_->current_blueprint(), *interner_, *arena_, instance_path, *reg,
                                   nullptr, icon_font_);
    }
    rebuild_snapshot();
}

void CanvasInput::rebuild_snapshot() {
    snapshot_ = editor::presentation::build_canvas_scene_snapshot(scene_, *interner_);
}

void CanvasInput::cancel_gesture() {
    if (state_uses_semantic_control_session()) {
        semantic_canvas_controller_.cancel();
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

size_t CanvasInput::find_wire_index(core::InternedId wire_id) const {
    if (wire_id.empty()) return SIZE_MAX;
    const auto& wires = host_->wires();
    for (size_t i = 0; i < wires.size(); ++i) {
        if (wires[i].id == wire_id) return i;
    }
    return SIZE_MAX;
}

size_t CanvasInput::find_node_index(core::InternedId node_id) const {
    if (node_id.empty()) return SIZE_MAX;
    const auto& nodes = host_->nodes();
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].semantic.id == node_id) return i;
    }
    return SIZE_MAX;
}
