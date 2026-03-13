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
#include "visual/node/node_utils.h"
#include "visual/snap.h"
#include "viewport/viewport.h"
#include "data/blueprint.h"
#include "data/node.h"
#include "debug.h"
#include <algorithm>
#include <cstdio>

// ============================================================================
// Construction
// ============================================================================

CanvasInput::CanvasInput(visual::Scene& scene, Viewport& viewport,
                         Blueprint& bp, const std::string& group_id)
    : scene_(scene), viewport_(viewport), bp_(bp), group_id_(group_id) {}

// ============================================================================
// Selection helpers
// ============================================================================

void CanvasInput::clear_selection() {
    selected_nodes_.clear();
    selected_wire_ = nullptr;
}

void CanvasInput::add_node_selection(visual::Widget* w) {
    if (!w) return;
    if (std::find(selected_nodes_.begin(), selected_nodes_.end(), w) == selected_nodes_.end())
        selected_nodes_.push_back(w);
}

bool CanvasInput::is_node_selected(visual::Widget* w) const {
    return std::find(selected_nodes_.begin(), selected_nodes_.end(), w) != selected_nodes_.end();
}

bool CanvasInput::selectNodeById(const std::string& node_id) {
    auto* widget = scene_.find(node_id);
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
        state_ == InputState::ResizingNode) {
        hovered_wire_ = nullptr;
        hovered_routing_point_ = nullptr;
        return;
    }

    // Check for wire/routing-point hover
    auto hit = visual::hit_test(scene_, world_pos);
    if (auto* h = std::get_if<visual::HitWire>(&hit)) {
        hovered_wire_ = h->wire;
        hovered_routing_point_ = nullptr;
    } else if (auto* h = std::get_if<visual::HitRoutingPoint>(&hit)) {
        hovered_wire_ = h->wire;
        hovered_routing_point_ = h->point;
    } else {
        hovered_wire_ = nullptr;
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
    for (auto* sel : selected_nodes_) {
        drag_offsets_.push_back(sel->worldPos() - primary_pos);
    }
}

void CanvasInput::enter_drag_routing_point(visual::Wire* wire, visual::RoutingPoint* rp, size_t rp_idx) {
    state_ = InputState::DraggingRoutingPoint;
    selected_wire_ = wire;
    rp_wire_ = wire;
    rp_point_ = rp;
    rp_index_ = rp_idx;
    drag_anchor_ = rp->worldPos();
}

void CanvasInput::enter_resize_node(visual::Widget* widget, ResizeCorner corner) {
    state_ = InputState::ResizingNode;
    clear_selection();
    add_node_selection(widget);
    resize_widget_ = widget;
    resize_corner_ = corner;
    resize_original_pos_ = widget->worldPos();
    resize_original_size_ = widget->size();
    drag_anchor_ = Pt(0, 0);  // accumulated delta
}

void CanvasInput::enter_create_wire(visual::Port* port, Pt port_pos) {
    state_ = InputState::CreatingWire;
    wire_start_port_ = port;
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
    wire_start_port_ = nullptr;
}

// ============================================================================
// on_mouse_down
// ============================================================================

InputResult CanvasInput::on_mouse_down(Pt screen_pos, MouseButton btn, Pt canvas_min, Modifiers mods) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    last_world_pos_ = world;

    if (btn == MouseButton::Left) {
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
                                     wire_match->anchor_pos, wire_match->fixed_side);
                return result;
            }
            Pt port_center = ph->port->worldPos() + Pt(visual::Port::RADIUS, visual::Port::RADIUS);
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
            // Check if click landed on Switch/VerticalToggle content area
            std::string node_id(hn->widget->id());
            const Node* node = bp_.find_node(node_id.c_str());
            if (node && (node->node_content.type == NodeContentType::Switch ||
                         node->node_content.type == NodeContentType::VerticalToggle)) {
                // Only toggle if click is inside the content widget bounds;
                // clicks on header / ports / footer should select/drag instead.
                bool in_content = false;
                if (auto* nw = dynamic_cast<visual::NodeWidget*>(hn->widget)) {
                    Bounds cb = nw->contentBounds();
                    Pt wpos = nw->worldPos();
                    float lx = world.x - wpos.x;
                    float ly = world.y - wpos.y;
                    in_content = cb.contains(lx, ly);
                }
                if (in_content) {
                    result.toggle_switch_node_id = node_id;
                    return result;
                }
            }
            enter_drag_node(hn->widget, false, mods.ctrl);
        } else if (auto* hrp = std::get_if<visual::HitRoutingPoint>(&hit)) {
            enter_drag_routing_point(hrp->wire, hrp->point, hrp->index);
        } else if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
            clear_selection();
            selected_wire_ = hw->wire;
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

            case InputState::DraggingNode: {
                drag_anchor_ = drag_anchor_ + world_delta;
                Pt snapped = editor_math::snap_to_grid(drag_anchor_, viewport_.grid_step);
                for (size_t i = 0; i < selected_nodes_.size(); i++) {
                    auto* widget = selected_nodes_[i];
                    Pt offset = (i < drag_offsets_.size()) ? drag_offsets_[i] : Pt(0, 0);
                    Pt new_pos = snapped + offset;

                    // Update visual widget
                    widget->setLocalPos(new_pos);

                    // Update data layer
                    std::string nid(widget->id());
                    Node* node = bp_.find_node(nid.c_str());
                    if (node) node->pos = new_pos;
                }
                break;
            }

            case InputState::DraggingRoutingPoint: {
                drag_anchor_ = drag_anchor_ + world_delta;
                Pt snapped = editor_math::snap_to_grid(drag_anchor_, viewport_.grid_step);

                // Update data layer
                size_t wire_idx = find_wire_index(rp_wire_);
                if (wire_idx < bp_.wires.size()) {
                    auto& wire = bp_.wires[wire_idx];
                    if (rp_index_ < wire.routing_points.size()) {
                        wire.routing_points[rp_index_] = snapped;
                    }
                }

                // Update visual widget
                if (rp_point_) {
                    rp_point_->setLocalPos(snapped);
                    if (rp_wire_) rp_wire_->invalidateGeometry();
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

            case InputState::ResizingNode: {
                drag_anchor_ = drag_anchor_ + world_delta;
                if (!resize_widget_) break;

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

                // Update visual widget
                resize_widget_->setLocalPos(new_pos);
                resize_widget_->setSize(new_size);

                // Update data layer
                std::string nid(resize_widget_->id());
                Node* node = bp_.find_node(nid.c_str());
                if (node) {
                    node->pos = new_pos;
                    node->size = new_size;
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
            size_t wire_idx = find_wire_index(hrp->wire);
            if (wire_idx < bp_.wires.size() && hrp->index < bp_.wires[wire_idx].routing_points.size()) {
                bp_.wires[wire_idx].routing_points.erase(
                    bp_.wires[wire_idx].routing_points.begin() + static_cast<long>(hrp->index));
                // Update visual: remove routing point widget
                if (hrp->wire) {
                    hrp->wire->removeRoutingPoint(hrp->index);
                }
            }
            return result;
        }
    }

    // 2. Node hit → open sub-window for Blueprint nodes (always allowed)
    if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
        std::string node_id(hn->widget->id());
        const Node* node = bp_.find_node(node_id.c_str());
        if (node && node->expandable) {
            result.open_sub_window = node_id;
            return result;
        }
    }

    // 3. Wire hit → add routing point (editing operation — skip in read-only)
    if (!read_only) {
        if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
            size_t wire_idx = find_wire_index(hw->wire);
            if (wire_idx < bp_.wires.size()) {
                // Find nearest segment and insert routing point
                auto& wire = bp_.wires[wire_idx];
                Pt snapped = editor_math::snap_to_grid(world, viewport_.grid_step);

                // Use the segment index from hit test for insertion position
                size_t insert_idx = hw->segment;
                wire.routing_points.insert(
                    wire.routing_points.begin() + static_cast<long>(insert_idx), snapped);

                // Update visual: add routing point widget
                if (hw->wire) {
                    hw->wire->addRoutingPoint(snapped, insert_idx);
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
            clear_selection();
            break;

        case Key::Delete:
        case Key::Backspace: {
            if (selected_nodes_.empty()) break;

            // Collect node indices for deletion (sorted descending for stable removal)
            std::vector<size_t> indices;
            for (auto* w : selected_nodes_) {
                size_t idx = find_node_index(std::string(w->id()));
                if (idx < bp_.nodes.size())
                    indices.push_back(idx);
            }
            std::sort(indices.begin(), indices.end(), std::greater<size_t>());

            visual::mutations::remove_nodes(scene_, bp_, indices);
            clear_selection();
            result.rebuild_simulation = true;
            break;
        }

        case Key::R:
            // TODO: Wire auto-routing with new system
            break;

        case Key::RightBracket:
            viewport_.grid_step_up();
            bp_.grid_step = viewport_.grid_step;
            break;

        case Key::LeftBracket:
            viewport_.grid_step_down();
            bp_.grid_step = viewport_.grid_step;
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
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    auto port_hit = visual::hit_test_ports(scene_, world);

    if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
        if (!wire_start_port_ || ph->port == wire_start_port_) return result;

        visual::Port* start_port = wire_start_port_;
        visual::Port* end_port = ph->port;

        // Check compatibility
        bool compatible = visual::Port::areSidesCompatible(start_port->side(), end_port->side());
        if (!compatible) return result;

        // Find owning node IDs by walking up the widget tree
        std::string start_node_id = start_port->parent() ? std::string(start_port->parent()->id()) : "";
        std::string end_node_id = end_port->parent() ? std::string(end_port->parent()->id()) : "";

        // Walk up to find the root node widget (port → row → column → node)
        auto find_node_widget_id = [](visual::Widget* port_widget) -> std::string {
            visual::Widget* w = port_widget->parent();
            while (w) {
                if (!w->id().empty() && !w->parent()) return std::string(w->id()); // root = node widget
                if (!w->id().empty()) {
                    // Check if this is a node widget (has no parent with an ID, or parent is scene root)
                    visual::Widget* p = w->parent();
                    if (!p || p->id().empty()) return std::string(w->id());
                }
                w = w->parent();
            }
            return {};
        };

        start_node_id = find_node_widget_id(start_port);
        end_node_id = find_node_widget_id(end_port);

        if (start_node_id.empty() || end_node_id.empty()) return result;

        // Check not same port
        if (start_node_id == end_node_id &&
            start_port->name() == end_port->name()) return result;

        WireEnd start_end(start_node_id.c_str(), start_port->name().c_str(), start_port->side());
        WireEnd end_end(end_node_id.c_str(), end_port->name().c_str(), end_port->side());

        std::string wire_id = visual::mutations::next_wire_id(bp_);
        ::Wire w = ::Wire::make(wire_id.c_str(), start_end, end_end);
        if (visual::mutations::add_wire(scene_, bp_, std::move(w), group_id_))
            result.rebuild_simulation = true;
    }
    return result;
}

InputResult CanvasInput::finish_wire_reconnection(Pt screen_pos, Pt canvas_min) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    auto port_hit = visual::hit_test_ports(scene_, world);

    bool reconnected = false;

    if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
        if (reconnect_wire_idx_ < bp_.wires.size()) {
            auto& wire = bp_.wires[reconnect_wire_idx_];
            const ::WireEnd& detached = reconnect_detach_start_ ? wire.start : wire.end;

            // Find the port's owning node
            auto find_node_widget_id = [](visual::Widget* port_widget) -> std::string {
                visual::Widget* w = port_widget->parent();
                while (w) {
                    if (!w->id().empty() && !w->parent()) return std::string(w->id());
                    if (!w->id().empty()) {
                        visual::Widget* p = w->parent();
                        if (!p || p->id().empty()) return std::string(w->id());
                    }
                    w = w->parent();
                }
                return {};
            };

            std::string port_node_id = find_node_widget_id(ph->port);

            // Try port swap first (for BusVisualNode)
            if (port_node_id == detached.node_id) {
                // Bus alias ports are named after the wire_id they represent.
                std::string target_wire_id(ph->port->name());
                if (visual::mutations::swap_wire_ports_on_bus(scene_, bp_,
                        port_node_id, target_wire_id, wire.id)) {
                    reconnected = true;
                    result.rebuild_simulation = true;
                }
            }

            // Fallback: standard wire reconnection
            if (!reconnected) {
                // Check if dropped back on same port.
                // For bus alias ports, the visual port name is the wire_id
                // while the blueprint stores "v" as port_name.
                std::string hit_port_name(ph->port->name());
                bool same_as_original = (port_node_id == detached.node_id &&
                                         (hit_port_name == detached.port_name ||
                                          hit_port_name == wire.id));
                if (same_as_original) return result;

                const ::WireEnd& fixed = reconnect_detach_start_ ? wire.end : wire.start;
                bool same_port = (port_node_id == fixed.node_id &&
                                  (hit_port_name == fixed.port_name ||
                                   hit_port_name == wire.id));
                bool compatible = !same_port &&
                    visual::Port::areSidesCompatible(ph->port->side(), reconnect_fixed_side_);

                if (compatible) {
                    ::WireEnd new_end(port_node_id.c_str(), ph->port->name().c_str(), ph->port->side());
                    visual::mutations::reconnect_wire(scene_, bp_,
                        reconnect_wire_idx_, reconnect_detach_start_, new_end, group_id_);
                    result.rebuild_simulation = true;
                    reconnected = true;
                }
            }
        }
    }

    if (!reconnected && reconnect_wire_idx_ < bp_.wires.size()) {
        visual::mutations::remove_wire(scene_, bp_, reconnect_wire_idx_);
        result.rebuild_simulation = true;
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
        // Only select node widgets (skip wires)
        if (root->renderLayer() == visual::RenderLayer::Wire) continue;
        // Only select nodes that have an ID (i.e., are actual node widgets)
        if (root->id().empty()) continue;

        // Verify this node belongs to our group
        const Node* node = bp_.find_node(std::string(root->id()).c_str());
        if (!node || node->group_id != group_id_) continue;

        Pt pos = root->worldPos();
        Pt sz = root->size();
        float cx = pos.x + sz.x / 2;
        float cy = pos.y + sz.y / 2;
        if (cx >= min_x && cx <= max_x && cy >= min_y && cy <= max_y)
            add_node_selection(root.get());
    }
}

// ============================================================================
// Utility helpers
// ============================================================================

size_t CanvasInput::find_wire_index(visual::Wire* wire) const {
    if (!wire) return SIZE_MAX;
    std::string wire_id(wire->id());
    for (size_t i = 0; i < bp_.wires.size(); ++i) {
        if (bp_.wires[i].id == wire_id) return i;
    }
    return SIZE_MAX;
}

size_t CanvasInput::find_node_index(const std::string& node_id) const {
    for (size_t i = 0; i < bp_.nodes.size(); ++i) {
        if (bp_.nodes[i].id == node_id) return i;
    }
    return SIZE_MAX;
}

std::optional<CanvasInput::WirePortMatch> CanvasInput::find_wire_on_port(visual::Port* port) const {
    if (!port) return std::nullopt;

    // Find the owning node widget
    auto find_node_widget_id = [](visual::Widget* port_widget) -> std::string {
        visual::Widget* w = port_widget->parent();
        while (w) {
            if (!w->id().empty() && !w->parent()) return std::string(w->id());
            if (!w->id().empty()) {
                visual::Widget* p = w->parent();
                if (!p || p->id().empty()) return std::string(w->id());
            }
            w = w->parent();
        }
        return {};
    };

    std::string port_node_id = find_node_widget_id(port);
    std::string port_name(port->name());

    // Determine if the port belongs to a Bus node
    const Node* hit_node = bp_.find_node(port_node_id.c_str());
    bool is_bus = hit_node && (hit_node->render_hint == "bus");

    for (size_t wi = 0; wi < bp_.wires.size(); ++wi) {
        const auto& w = bp_.wires[wi];

        bool match_start = false;
        bool match_end = false;

        if (is_bus) {
            // Bus alias ports are named after the wire ID they represent.
            // Match by both node_id AND wire_id == port_name.
            match_start = (w.start.node_id == port_node_id && w.id == port_name);
            match_end   = (w.end.node_id == port_node_id && w.id == port_name);
        } else {
            match_start = (w.start.node_id == port_node_id &&
                           w.start.port_name == port_name);
            match_end   = (w.end.node_id == port_node_id &&
                           w.end.port_name == port_name);
        }

        if (match_start || match_end) {
            bool detach_start = match_start;

            Pt anchor_pos;
            PortSide fixed_side;
            if (detach_start) {
                fixed_side = w.end.side;
                // Anchor = nearest routing point or far port position
                if (!w.routing_points.empty()) {
                    anchor_pos = w.routing_points.front();
                } else {
                    // Look up far port position from visual widget
                    auto* end_widget = scene_.find(w.end.node_id);
                    if (end_widget) {
                        auto* end_port = end_widget->portByName(w.end.port_name, w.id);
                        if (end_port)
                            anchor_pos = end_port->worldPos() + Pt(visual::Port::RADIUS, visual::Port::RADIUS);
                    }
                }
            } else {
                fixed_side = w.start.side;
                if (!w.routing_points.empty()) {
                    anchor_pos = w.routing_points.back();
                } else {
                    auto* start_widget = scene_.find(w.start.node_id);
                    if (start_widget) {
                        auto* start_port = start_widget->portByName(w.start.port_name, w.id);
                        if (start_port)
                            anchor_pos = start_port->worldPos() + Pt(visual::Port::RADIUS, visual::Port::RADIUS);
                    }
                }
            }

            return WirePortMatch{wi, detach_start, anchor_pos, fixed_side};
        }
    }
    return std::nullopt;
}
