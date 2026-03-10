#include "input/canvas_input.h"
#include "visual/scene/scene.h"
#include "visual/scene/wire_manager.h"
#include "visual/hittest.h"
#include "visual/trigonometry.h"
#include "visual/node/node.h"
#include "debug.h"
#include <algorithm>
#include <cstdio>

// ============================================================================
// Construction
// ============================================================================

CanvasInput::CanvasInput(VisualScene& scene, WireManager& wire_manager)
    : scene_(scene), wire_mgr_(wire_manager) {}

// ============================================================================
// Selection helpers
// ============================================================================

void CanvasInput::clear_selection() {
    selected_nodes_.clear();
    selected_wire_.reset();
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
        state_ == InputState::ResizingNode) {
        hovered_wire_.reset();
        return;
    }

    // Check for wire hover
    HitResult hit = scene_.hitTest(world_pos);
    if (hit.type == HitType::Wire || hit.type == HitType::RoutingPoint) {
        hovered_wire_ = hit.wire_index;
    } else {
        hovered_wire_.reset();
    }
}

void CanvasInput::add_node_selection(size_t idx) {
    if (std::find(selected_nodes_.begin(), selected_nodes_.end(), idx) == selected_nodes_.end())
        selected_nodes_.push_back(idx);
}

bool CanvasInput::is_node_selected(size_t idx) const {
    return std::find(selected_nodes_.begin(), selected_nodes_.end(), idx) != selected_nodes_.end();
}

bool CanvasInput::selectNodeById(const std::string& node_id) {
    const auto& nodes = scene_.nodes();
    for (size_t i = 0; i < nodes.size(); i++) {
        if (nodes[i].id == node_id) {
            clear_selection();
            add_node_selection(i);
            // Center viewport on the node
            const Node& n = nodes[i];
            Pt center(n.pos.x + n.size.x * 0.5f, n.pos.y + n.size.y * 0.5f);
            // Use a reasonable default window size for centering; actual window
            // dimension will be applied by the caller if needed.
            scene_.viewport().centerOn(center, 800.0f, 600.0f);
            return true;
        }
    }
    return false;
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

void CanvasInput::enter_drag_node(size_t node_index, bool add_to_selection, bool ctrl) {
    if (!ctrl) clear_selection();
    add_node_selection(node_index);

    auto* primary = scene_.cache().getOrCreate(scene_.nodes()[node_index], scene_.wires());
    Pt primary_pos = primary->getPosition();

    state_ = InputState::DraggingNode;
    drag_anchor_ = primary_pos;
    drag_offsets_.clear();
    for (size_t idx : selected_nodes_) {
        if (idx < scene_.nodes().size()) {
            auto* vis = scene_.cache().getOrCreate(scene_.nodes()[idx], scene_.wires());
            drag_offsets_.push_back(vis->getPosition() - primary_pos);
        }
    }
}

void CanvasInput::enter_drag_routing_point(size_t wire_idx, size_t rp_idx) {
    state_ = InputState::DraggingRoutingPoint;
    selected_wire_ = wire_idx;
    rp_wire_ = wire_idx;
    rp_index_ = rp_idx;
    drag_anchor_ = scene_.wires()[wire_idx].routing_points[rp_idx];
}

void CanvasInput::enter_resize_node(size_t node_index, ResizeCorner corner) {
    state_ = InputState::ResizingNode;
    clear_selection();
    add_node_selection(node_index);
    resize_node_idx_ = node_index;
    resize_corner_ = corner;
    auto* vis = scene_.cache().getOrCreate(scene_.nodes()[node_index], scene_.wires());
    resize_original_pos_ = vis->getPosition();
    resize_original_size_ = vis->getSize();
    drag_anchor_ = Pt(0, 0);  // accumulated delta
}

void CanvasInput::enter_create_wire(const std::string& node_id, const std::string& port_name,
                                    PortSide side, Pt port_pos) {
    state_ = InputState::CreatingWire;
    wire_start_node_ = node_id;
    wire_start_port_ = port_name;
    wire_start_side_ = side;
    wire_start_pos_ = port_pos;
}

void CanvasInput::enter_reconnect_wire(size_t wire_idx, bool detach_start,
                                       Pt anchor_pos, PortSide fixed_side) {
    state_ = InputState::ReconnectingWire;
    reconnect_wire_idx_ = wire_idx;
    reconnect_detach_start_ = detach_start;
    reconnect_anchor_pos_ = anchor_pos;
    reconnect_fixed_side_ = fixed_side;
}

void CanvasInput::enter_marquee(Pt world_pos) {
    state_ = InputState::MarqueeSelect;
    marquee_start_ = world_pos;
    marquee_end_ = world_pos;
}

void CanvasInput::leave_state() {
    state_ = InputState::Idle;
    drag_offsets_.clear();
    wire_start_node_.clear();
    wire_start_port_.clear();
}

// ============================================================================
// on_mouse_down
// ============================================================================

InputResult CanvasInput::on_mouse_down(Pt screen_pos, MouseButton btn, Pt canvas_min, Modifiers mods) {
    InputResult result;
    Pt world = scene_.viewport().screen_to_world(screen_pos, canvas_min);
    last_world_pos_ = world;

    if (btn == MouseButton::Left) {
        // 1. Check ports first (wire creation / reconnection)
        HitResult port_hit = scene_.hitTestPorts(world);
        if (port_hit.type == HitType::Port) {
            auto wire_match = wire_mgr_.findWireOnPort(port_hit);
            if (wire_match) {
                enter_reconnect_wire(wire_match->wire_index, wire_match->detach_start,
                                     wire_match->anchor_pos, wire_match->fixed_side);
                return result;
            }
            enter_create_wire(port_hit.port_node_id, port_hit.port_name,
                              port_hit.port_side, port_hit.port_position);
            return result;
        }

        // 2. General hit test
        HitResult hit = scene_.hitTest(world);

        if (mods.alt) {
            // Alt+click → marquee selection
            enter_marquee(world);
        } else if (hit.type == HitType::ResizeHandle) {
            enter_resize_node(hit.node_index, hit.resize_corner);
        } else if (hit.type == HitType::Node) {
            // Check if click landed on Switch/VerticalToggle content area
            const auto& node = scene_.nodes()[hit.node_index];
            if (node.node_content.type == NodeContentType::Switch ||
                node.node_content.type == NodeContentType::VerticalToggle) {
                auto* visual = scene_.cache().getOrCreate(node, scene_.wires());
                Bounds cb = visual->getContentBounds();
                float local_x = world.x - node.pos.x;
                float local_y = world.y - node.pos.y;
                if (cb.w > 0 && cb.contains(local_x, local_y)) {
                    result.toggle_switch_node_id = node.id;
                    return result;
                }
            }
            enter_drag_node(hit.node_index, false, mods.ctrl);
        } else if (hit.type == HitType::RoutingPoint) {
            if (hit.wire_index < scene_.wires().size() &&
                hit.routing_point_index < scene_.wires()[hit.wire_index].routing_points.size()) {
                enter_drag_routing_point(hit.wire_index, hit.routing_point_index);
            }
        } else if (hit.type == HitType::Wire) {
            clear_selection();
            selected_wire_ = hit.wire_index;
        } else {
            // Empty space → panning
            clear_selection();
            enter_panning();
        }
    } else if (btn == MouseButton::Right) {
        HitResult hit = scene_.hitTest(world);
        if (hit.type == HitType::Node) {
            result.show_node_context_menu = true;
            result.context_menu_node_index = hit.node_index;
        } else if (hit.type == HitType::None) {
            result.show_context_menu = true;
            result.context_menu_pos = world;
        }
    }
    return result;
}

// ============================================================================
// on_mouse_drag
// ============================================================================

InputResult CanvasInput::on_mouse_drag(MouseButton btn, Pt screen_delta, Pt canvas_min) {
    InputResult result;
    float zoom = scene_.viewport().zoom;
    Pt world_delta(screen_delta.x / zoom, screen_delta.y / zoom);

    if (btn == MouseButton::Left) {
        switch (state_) {
            case InputState::Panning:
                scene_.viewport().pan.x -= world_delta.x;
                scene_.viewport().pan.y -= world_delta.y;
                last_world_pos_ = last_world_pos_ + world_delta;
                break;

            case InputState::DraggingNode: {
                drag_anchor_ = drag_anchor_ + world_delta;
                Pt snapped = editor_math::snap_to_grid(drag_anchor_, scene_.gridStep());
                for (size_t i = 0; i < selected_nodes_.size(); i++) {
                    size_t idx = selected_nodes_[i];
                    if (idx < scene_.nodes().size()) {
                        Pt offset = (i < drag_offsets_.size()) ? drag_offsets_[i] : Pt(0, 0);
                        Pt new_pos = snapped + offset;
                        scene_.nodes()[idx].pos = new_pos;
                        auto* vis = scene_.cache().getOrCreate(scene_.nodes()[idx], scene_.wires());
                        if (vis) vis->setPosition(new_pos);
                    }
                }
                break;
            }

            case InputState::DraggingRoutingPoint: {
                drag_anchor_ = drag_anchor_ + world_delta;
                Pt snapped = editor_math::snap_to_grid(drag_anchor_, scene_.gridStep());
                if (rp_wire_ < scene_.wires().size()) {
                    auto& wire = scene_.wires()[rp_wire_];
                    if (rp_index_ < wire.routing_points.size())
                        wire.routing_points[rp_index_] = snapped;
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

            case InputState::ResizingNode: {
                drag_anchor_ = drag_anchor_ + world_delta;
                if (resize_node_idx_ >= scene_.nodes().size()) break;

                float grid = scene_.gridStep();
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

                // Enforce minimum size
                float min_w = editor_constants::MIN_GROUP_WIDTH;
                float min_h = editor_constants::MIN_GROUP_HEIGHT;
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

                auto& node_data = scene_.nodes()[resize_node_idx_];
                node_data.pos = new_pos;
                node_data.size = new_size;
                auto* vis = scene_.cache().getOrCreate(node_data, scene_.wires());
                if (vis) {
                    vis->setPosition(new_pos);
                    vis->setSize(new_size);
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

            default:
                break;
        }
        leave_state();
        scene_.invalidateSpatialGrid();
    }
    return result;
}

// ============================================================================
// on_scroll
// ============================================================================

InputResult CanvasInput::on_scroll(float delta, Pt screen_pos, Pt canvas_min) {
    scene_.viewport().zoom_at(delta, screen_pos, canvas_min);
    return {};
}

// ============================================================================
// on_double_click
// ============================================================================

InputResult CanvasInput::on_double_click(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = scene_.viewport().screen_to_world(screen_pos, canvas_min);

    HitResult hit = scene_.hitTest(world);

    // 1. Routing-point removal (hitTest returns RoutingPoint with higher priority than Wire)
    if (hit.type == HitType::RoutingPoint) {
        wire_mgr_.removeRoutingPoint(hit.wire_index, hit.routing_point_index);
        return result;
    }

    // 2. Node hit → open sub-window for Blueprint nodes
    if (hit.type == HitType::Node) {
        const auto& node = scene_.nodes()[hit.node_index];
        if (node.expandable) {
            result.open_sub_window = node.id;
            return result;
        }
    }

    // 3. Wire hit → add routing point
    if (hit.type == HitType::Wire) {
        wire_mgr_.addRoutingPoint(hit.wire_index, world);
    }

    return result;
}

// ============================================================================
// on_key
// ============================================================================

InputResult CanvasInput::on_key(Key key) {
    InputResult result;

    switch (key) {
        case Key::Escape:
            clear_selection();
            break;

        case Key::Delete:
        case Key::Backspace: {
            if (selected_nodes_.empty()) break;
            std::sort(selected_nodes_.begin(), selected_nodes_.end(), std::greater<size_t>());
            scene_.removeNodes(selected_nodes_);
            clear_selection();
            result.rebuild_simulation = true;
            break;
        }

        case Key::R:
            if (selected_wire_.has_value()) {
                wire_mgr_.routeWire(*selected_wire_);
            }
            break;

        case Key::RightBracket:
            scene_.gridStepUp();
            break;

        case Key::LeftBracket:
            scene_.gridStepDown();
            break;

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
    Pt world = scene_.viewport().screen_to_world(screen_pos, canvas_min);
    HitResult port_hit = scene_.hitTestPorts(world);

    if (port_hit.type == HitType::Port) {
        bool same_port = WireManager::isSamePort(
            port_hit.port_node_id, port_hit.port_name,
            wire_start_node_, wire_start_port_);
        bool compatible = !same_port &&
                          WireManager::canConnect(port_hit.port_side, wire_start_side_);

        if (compatible) {
            WireEnd start_end(wire_start_node_.c_str(), wire_start_port_.c_str(), wire_start_side_);
            WireEnd end_end(port_hit.port_node_id.c_str(), port_hit.port_name.c_str(), port_hit.port_side);

            Wire w = Wire::make(scene_.nextWireId().c_str(), start_end, end_end);
            if (scene_.addWire(std::move(w)))
                result.rebuild_simulation = true;
        }
    }
    return result;
}

InputResult CanvasInput::finish_wire_reconnection(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = scene_.viewport().screen_to_world(screen_pos, canvas_min);
    HitResult port_hit = scene_.hitTestPorts(world);

    bool reconnected = false;

    if (port_hit.type == HitType::Port && reconnect_wire_idx_ < scene_.wires().size()) {
        auto& wire = scene_.wires()[reconnect_wire_idx_];
        const WireEnd& detached = reconnect_detach_start_ ? wire.start : wire.end;

        // NEW: Try port swap first (for BusVisualNode)
        // If dragging a wire end within the same bus node, swap ports instead of reconnecting.
        // Use wire.id (the wire being dragged), NOT detached.port_name ("v") — bus wires all
        // connect to the logical "v" port, but each has a unique wire ID that identifies its
        // alias port slot. Passing "v" as wire_id_b would fail to find a matching wire.
        if (port_hit.port_node_id == detached.node_id) {
            if (scene_.swapWirePortsOnBus(port_hit.port_node_id, port_hit.port_wire_id, wire.id)) {
                // Port swap successful! No wire reconnection needed.
                reconnected = true;
                result.rebuild_simulation = true;
            }
        }

        // Fallback: standard wire reconnection if swap not handled
        if (!reconnected) {
            // Dropped back on same port?
            bool same_as_original;
            if (!port_hit.port_wire_id.empty())
                same_as_original = (port_hit.port_node_id == detached.node_id &&
                                    port_hit.port_wire_id == wire.id);
            else
                same_as_original = (port_hit.port_node_id == detached.node_id &&
                                    port_hit.port_name == detached.port_name);

            if (same_as_original)
                return result;  // no change

            const WireEnd& fixed = reconnect_detach_start_ ? wire.end : wire.start;
            bool same_port = WireManager::isSamePort(
                port_hit.port_node_id, port_hit.port_name,
                fixed.node_id, fixed.port_name);
            bool compatible = !same_port &&
                              WireManager::canConnect(port_hit.port_side, reconnect_fixed_side_);

            if (compatible) {
                WireEnd new_end(port_hit.port_node_id.c_str(), port_hit.port_name.c_str(), port_hit.port_side);
                scene_.reconnectWire(reconnect_wire_idx_, reconnect_detach_start_, new_end);
                result.rebuild_simulation = true;
                reconnected = true;
            }
        }
    }

    if (!reconnected && reconnect_wire_idx_ < scene_.wireCount()) {
        scene_.removeWire(reconnect_wire_idx_);
        result.rebuild_simulation = true;
    }
    return result;
}

// ============================================================================
// Marquee finisher
// ============================================================================

// [BUG-c3d4] finish_marquee must filter by group_id — was selecting nodes from ALL groups
void CanvasInput::finish_marquee() {
    float min_x = std::min(marquee_start_.x, marquee_end_.x);
    float max_x = std::max(marquee_start_.x, marquee_end_.x);
    float min_y = std::min(marquee_start_.y, marquee_end_.y);
    float max_y = std::max(marquee_start_.y, marquee_end_.y);

    for (size_t i = 0; i < scene_.nodes().size(); i++) {
        if (!scene_.ownsNode(scene_.nodes()[i])) continue;
        auto* vis = scene_.cache().getOrCreate(scene_.nodes()[i], scene_.wires());
        Pt pos = vis->getPosition();
        Pt sz = vis->getSize();
        float cx = pos.x + sz.x / 2;
        float cy = pos.y + sz.y / 2;
        if (cx >= min_x && cx <= max_x && cy >= min_y && cy <= max_y)
            add_node_selection(i);
    }
}
