#include "visual/node/node.h"
#include "data/node.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "layout_constants.h"
#include <spdlog/spdlog.h>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>

// ImGui is needed for editor builds (for float value math, not render flags).
// DRAW_CORNERS_* constants come from layout_constants.h and match ImDrawFlags_* values.
#ifndef EDITOR_TESTING
#include <imgui.h>
#endif

// ============================================================================
// Constants
// ============================================================================

namespace {

// BUGFIX [8d4e6a] Removed hardcoded GRID_STEP=16 snap in VisualNode constructor.
// Node positions are already snapped by callers (CanvasInput, add_component, persist loader).
// Re-snapping here to 16px silently moved nodes placed on 4/8/12px grids.
// Port position snapping still uses 16px (internal layout, not user-facing).

Pt snap_to_grid(Pt pos) {
    return Pt(
        std::round(pos.x / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID,
        std::round(pos.y / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID
    );
}

Pt snap_size_to_grid(Pt size) {
    return Pt(
        std::ceil(size.x / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID,
        std::ceil(size.y / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID
    );
}

}  // anonymous namespace

// ============================================================================
// VisualNode
// ============================================================================

VisualNode::VisualNode(const Node& node)
    : position_(node.pos)  // BUGFIX [8d4e6a] Use position as-is (already snapped by caller)
    , size_(node.size)
    , node_id_(node.id)
    , ports_()
    , name_(node.name)
    , type_name_(node.type_name)
    , node_content_(node.node_content)
    , custom_color_(node.color)
{
    buildLayout(node);

    // Auto-size node to fit all widgets (BOTH width AND height)
    Pt preferred = layout_.getPreferredSize(nullptr);
    preferred.x = std::max(preferred.x, editor_constants::MIN_NODE_WIDTH);

    // Use size_explicitly_set flag to determine if user intended this size
    bool has_explicit_width = node.size_explicitly_set;
    bool has_explicit_height = node.size_explicitly_set;

    // Debug: log auto-size decisions for gauge nodes
    if (node.node_content.type == NodeContentType::Gauge) {
        spdlog::info("[autosize] GAUGE Node '{}': JSON size={:.1f}x{:.1f}, explicit={}/{}, preferred={:.1f}x{:.1f}",
                     node.name, node.size.x, node.size.y,
                     has_explicit_width, has_explicit_height, preferred.x, preferred.y);
    }

    // Auto-size logic:
    // 1. If no explicit size → auto-size BOTH dimensions to preferred (with grid snap)
    // 2. If explicit size but smaller than preferred → use explicit (with warning), no snap
    // 3. If explicit size and larger than preferred → use explicit as-is, no snap

    if (!has_explicit_width && !has_explicit_height) {
        // No explicit size at all: auto-size both dimensions with grid snap
        size_ = snap_size_to_grid(preferred);
        spdlog::debug("[autosize] Node '{}' auto-sized to {:.1f}x{:.1f}",
                      node.name, size_.x, size_.y);
    } else {
        // At least one dimension is explicit
        if (!has_explicit_width) {
            size_.x = snap_size_to_grid(Pt(preferred.x, 0)).x;
        } else if (node.size.x < preferred.x) {
            spdlog::warn("[autosize] Node '{}' explicit width {:.1f}px is too small "
                         "(minimum required: {:.1f}px). Content may be clipped.",
                         node.name, node.size.x, preferred.x);
        } else {
            size_.x = node.size.x;
        }

        if (!has_explicit_height) {
            size_.y = snap_size_to_grid(Pt(0, preferred.y)).y;
        } else if (node.size.y < preferred.y) {
            spdlog::warn("[autosize] Node '{}' explicit height {:.1f}px is too small "
                         "(minimum required: {:.1f}px). Content may be clipped.",
                         node.name, node.size.y, preferred.y);
        } else {
            size_.y = node.size.y;
        }
    }

    // Recalculate layout with final size
    layout_.layout(size_.x, size_.y);

    // Build VisualPort objects from layout positions
    buildPorts(node);
}

void VisualNode::setPosition(Pt pos) {
    Pt delta(pos.x - position_.x, pos.y - position_.y);
    position_ = pos;
    for (auto& port : ports_) {
        Pt old = port.worldPosition();
        port.setWorldPosition(Pt(old.x + delta.x, old.y + delta.y));
    }
}

// [t4u5v6w7] Snap size to grid so bottom-right corner stays grid-aligned.
void VisualNode::setSize(Pt size) {
    size_ = snap_size_to_grid(size);
}

void VisualNode::buildLayout(const Node& node) {
    // Header (with rounded top corners matching NODE_ROUNDING)
    layout_.addChild(std::make_unique<HeaderWidget>(
        name_, render_theme::COLOR_HEADER_FILL, editor_constants::NODE_ROUNDING));

    // Port rows: pair inputs and outputs
    size_t max_ports = std::max(node.inputs.size(), node.outputs.size());
    port_slots_.clear();
    for (size_t i = 0; i < max_ports; i++) {
        std::string left_name = (i < node.inputs.size()) ? node.inputs[i].name : "";
        std::string right_name = (i < node.outputs.size()) ? node.outputs[i].name : "";
        an24::PortType left_type = (i < node.inputs.size()) ? node.inputs[i].type : an24::PortType::Any;
        an24::PortType right_type = (i < node.outputs.size()) ? node.outputs[i].type : an24::PortType::Any;

        auto row = std::make_unique<Row>();

        if (!left_name.empty()) {
            uint32_t left_color = render_theme::get_port_color(left_type);
            row->addChild(std::make_unique<Container>(
                std::make_unique<Circle>(editor_constants::PORT_RADIUS, left_color),
                Edges{-editor_constants::PORT_RADIUS, 0, editor_constants::PORT_LABEL_GAP, 0}
            ));
            row->addChild(std::make_unique<Label>(left_name, editor_constants::PORT_LABEL_FONT_SIZE, editor_constants::PORT_LABEL_COLOR));
        }

        // Flexible gap between left and right labels
        if (!left_name.empty() && !right_name.empty()) {
            float half_gap = editor_constants::PORT_MIN_GAP / 2.0f;
            auto gap = std::make_unique<Container>(
                std::make_unique<Spacer>(),
                Edges{half_gap, 0, half_gap, 0}
            );
            gap->setFlexible(true);
            row->addChild(std::move(gap));
        } else {
            row->addChild(std::make_unique<Spacer>());
        }

        if (!right_name.empty()) {
            row->addChild(std::make_unique<Label>(right_name, editor_constants::PORT_LABEL_FONT_SIZE, editor_constants::PORT_LABEL_COLOR));
            uint32_t right_color = render_theme::get_port_color(right_type);
            row->addChild(std::make_unique<Container>(
                std::make_unique<Circle>(editor_constants::PORT_RADIUS, right_color),
                Edges{editor_constants::PORT_LABEL_GAP, 0, -editor_constants::PORT_RADIUS, 0}
            ));
        }

        // Wrap row in padding container to achieve PORT_ROW_HEIGHT
        float inner_h = row->getPreferredSize(nullptr).y;
        float pad = std::max(0.0f, (editor_constants::PORT_ROW_HEIGHT - inner_h) / 2.0f);
        auto row_container = std::make_unique<Container>(
            std::move(row),
            Edges{0, pad, 0, pad}
        );

        auto* container_ptr = layout_.addChild(std::move(row_container));

        if (!left_name.empty()) {
            port_slots_.push_back({container_ptr, left_name, true, left_type});
        }
        if (!right_name.empty()) {
            port_slots_.push_back({container_ptr, right_name, false, right_type});
        }
    }

    // Content area (flexible, takes remaining space)
    if (node_content_.type != NodeContentType::None) {
        if (node_content_.type == NodeContentType::Gauge) {
            layout_.addChild(std::make_unique<VoltmeterWidget>(
                node_content_.value,
                node_content_.min,
                node_content_.max,
                node_content_.unit
            ));
        } else {
            // Content area: sized by label text, with margins to avoid overlapping port circles
            float margin = editor_constants::PORT_RADIUS + editor_constants::PORT_LABEL_GAP;
            std::unique_ptr<Widget> content_inner;
            if (!node_content_.label.empty()) {
                // Use a Label for sizing (text gets overdrawn by ImGui overlay)
                content_inner = std::make_unique<Label>(node_content_.label, 10.0f, 0x00000000);
            } else {
                content_inner = std::make_unique<Spacer>();
            }
            auto content_container = std::make_unique<Container>(
                std::move(content_inner),
                Edges{margin, 0, margin, 0}
            );
            content_container->setFlexible(true);
            content_widget_ = layout_.addChild(std::move(content_container));
        }
    }

    // Type name at bottom
    layout_.addChild(std::make_unique<TypeNameWidget>(type_name_));
}

void VisualNode::buildPorts(const Node& node) {
    ports_.clear();
    for (const auto& p : node.inputs) {
        VisualPort vp(p.name, PortSide::Input, p.type);
        for (const auto& slot : port_slots_) {
            if (slot.is_left && slot.name == p.name) {
                float port_y = position_.y + slot.row_container->y() + slot.row_container->height() / 2;
                float port_x = position_.x;
                vp.setWorldPosition(Pt(port_x, port_y));
                break;
            }
        }
        ports_.push_back(std::move(vp));
    }
    for (const auto& p : node.outputs) {
        VisualPort vp(p.name, PortSide::Output, p.type);
        for (const auto& slot : port_slots_) {
            if (!slot.is_left && slot.name == p.name) {
                float port_y = position_.y + slot.row_container->y() + slot.row_container->height() / 2;
                float port_x = position_.x + size_.x;
                vp.setWorldPosition(Pt(port_x, port_y));
                break;
            }
        }
        ports_.push_back(std::move(vp));
    }
}

const VisualPort* VisualNode::getPort(const std::string& name) const {
    for (const auto& p : ports_) {
        if (p.name() == name) return &p;
    }
    return nullptr;
}

const VisualPort* VisualNode::getPort(size_t index) const {
    return index < ports_.size() ? &ports_[index] : nullptr;
}

std::vector<std::string> VisualNode::getPortNames() const {
    std::vector<std::string> names;
    for (const auto& p : ports_) names.push_back(p.name());
    return names;
}

const VisualPort* VisualNode::resolveWirePort(const std::string& port_name,
                                               const char* wire_id) const {
    (void)wire_id;  // Default: ignore wire_id
    return getPort(port_name);
}

void VisualNode::connectWire(const Wire&) {}
void VisualNode::disconnectWire(const Wire&) {}
void VisualNode::recalculatePorts() {}

void VisualNode::updateNodeContent(const NodeContent& content) {
    node_content_ = content;
    // Propagate updated value to VoltmeterWidget so gauge animation works.
    // Without this, the widget retains its construction-time value forever.
    if (node_content_.type == NodeContentType::Gauge) {
        for (size_t i = 0; i < layout_.childCount(); i++) {
            if (auto* vw = dynamic_cast<VoltmeterWidget*>(layout_.child(i))) {
                vw->setValue(node_content_.value);
                break;
            }
        }
    }
}

void VisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                        bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    float rounding = editor_constants::NODE_ROUNDING * vp.zoom;
    uint32_t border_color = is_selected ? render_theme::COLOR_SELECTED : render_theme::COLOR_BUS_BORDER;

    // === BACKGROUND: Full node with all corners rounded ===
    uint32_t fill = custom_color_.has_value()
        ? custom_color_->to_uint32()
        : render_theme::COLOR_BODY_FILL;
    dl->add_rect_filled_with_rounding(screen_min, screen_max, fill, rounding);

    // === Render all content ===
    // HeaderWidget draws its own header background with rounded top corners.
    layout_.render(dl, screen_min, vp.zoom);

    // === BORDER: Full outline with all corners rounded ===
    dl->add_rect_with_rounding_corners(
        screen_min, screen_max, border_color, rounding,
        editor_constants::DRAW_CORNERS_ALL, 1.0f);

    // === PORTS: Draw on top of everything for highest z-index ===
    float port_radius = editor_constants::PORT_RADIUS * vp.zoom;
    for (const auto& port : ports_) {
        Pt screen_pos = vp.world_to_screen(port.worldPosition(), canvas_min);
        uint32_t port_color = render_theme::get_port_color(port.type());
        dl->add_circle_filled(screen_pos, port_radius, port_color, 8);
    }
}

Bounds VisualNode::getContentBounds() const {
    if (!content_widget_) return {};

    // content_widget_ is a Container with margins around a Spacer.
    // The Container's child (the Spacer) has the actual content area.
    auto* container = dynamic_cast<const Container*>(content_widget_);
    if (!container || !container->child()) return {};

    const Widget* spacer = container->child();
    return {
        content_widget_->x() + spacer->x(),
        content_widget_->y() + spacer->y(),
        spacer->width(),
        spacer->height()
    };
}

// ============================================================================
// BusVisualNode
// ============================================================================

BusVisualNode::BusVisualNode(const Node& node, BusOrientation orientation,
                             const std::vector<Wire>& wires)
    : VisualNode(node)
    , orientation_(orientation)
{
    // [2.5] Store only wires connected to this bus (not the full blueprint)
    for (const auto& w : wires) {
        if (w.start.node_id == node_id_ || w.end.node_id == node_id_)
            wires_.push_back(w);
    }
    // Reset size — base constructor may have auto-sized for layout that Bus doesn't use
    size_ = snap_size_to_grid(node.size);
    // Override base class ports — Bus has its own port layout
    distributePortsInRow(wires_);
}

void BusVisualNode::distributePortsInRow(const std::vector<Wire>& wires) {
    ports_.clear();

    // [i3j4k5l6] Wire alias ports FIRST (indices 0..N-1).
    // The logical "v" port goes at the end as a "connect new wire here" target.
    for (const auto& w : wires) {
        if (w.start.node_id == node_id_ || w.end.node_id == node_id_) {
            VisualPort vp(w.id, PortSide::InOut, an24::PortType::V, "v");
            vp.setWorldPosition(calculatePortPosition(ports_.size()));
            ports_.push_back(std::move(vp));
        }
    }

    // Logical "v" port at the end (for new wire connections)
    VisualPort v_port("v", PortSide::InOut, an24::PortType::V);
    v_port.setWorldPosition(calculatePortPosition(ports_.size()));
    ports_.push_back(std::move(v_port));

    // Resize if we have wires (preserves initial size when bus is empty)
    if (!wires.empty()) {
        size_ = calculateBusSize(ports_.size());
    }

    // Recalculate all port positions after resize
    for (size_t i = 0; i < ports_.size(); i++) {
        ports_[i].setWorldPosition(calculatePortPosition(i));
    }

    // [2.5] Bus invariant: ports = alias ports (one per connected wire) + 1 logical "v" port
    assert(ports_.size() == wires_.size() + 1);
}

Pt BusVisualNode::calculateBusSize(size_t port_count) const {
    Pt size;
    if (orientation_ == BusOrientation::Horizontal) {
        size = Pt((port_count + 2) * editor_constants::PORT_LAYOUT_GRID, editor_constants::PORT_LAYOUT_GRID * 2);
    } else {
        size = Pt(editor_constants::PORT_LAYOUT_GRID * 2, (port_count + 2) * editor_constants::PORT_LAYOUT_GRID);
    }
    return snap_size_to_grid(size);
}

Pt BusVisualNode::calculatePortPosition(size_t index) const {
    if (ports_.empty() && index == 0) {
        return snap_to_grid(Pt(position_.x + size_.x / 2, position_.y + size_.y / 2));
    }

    bool ports_on_bottom = (size_.x > size_.y);
    float step = editor_constants::PORT_LAYOUT_GRID;

    if (ports_on_bottom) {
        float x = position_.x + step * (index + 1);
        float y = position_.y + size_.y;
        return snap_to_grid(Pt(x, y));
    } else {
        float x = position_.x + size_.x;
        float y = position_.y + step * (index + 1);
        return snap_to_grid(Pt(x, y));
    }
}

const VisualPort* BusVisualNode::resolveWirePort(const std::string& port_name,
                                                  const char* wire_id) const {
    // [e7b4c2d5] When wire_id is provided and port_name is "v", resolve by wire_id FIRST.
    // Alias ports have name == wire_id, target_port == "v".
    if (port_name == "v" && wire_id != nullptr) {
        for (const auto& p : ports_) {
            if (p.name() == wire_id) return &p;
        }
    }

    return getPort(port_name);
}

void BusVisualNode::connectWire(const Wire& wire) {
    if (wire.start.node_id == node_id_ || wire.end.node_id == node_id_) {
        // [b8d2e4f1] Add wire to wires_ before redistributing
        wires_.push_back(wire);
        distributePortsInRow(wires_);
        size_ = calculateBusSize(ports_.size());
    }
}

void BusVisualNode::disconnectWire(const Wire& wire) {
    // [b8d2e4f1] Remove wire from wires_ to keep in sync
    wires_.erase(
        std::remove_if(wires_.begin(), wires_.end(),
            [&](const Wire& w) { return w.id == wire.id; }),
        wires_.end());
    // Remove the alias port for this wire
    std::string port_name = wire.id;
    ports_.erase(
        std::remove_if(ports_.begin(), ports_.end(),
            [&](const VisualPort& p) { return p.name() == port_name; }),
        ports_.end());
    // Resize and recalculate positions
    size_ = calculateBusSize(ports_.size());
    for (size_t i = 0; i < ports_.size(); i++) {
        ports_[i].setWorldPosition(calculatePortPosition(i));
    }
    // [2.5] Bus invariant: ports = wires + 1 logical "v" port
    assert(ports_.size() == wires_.size() + 1);
}

void BusVisualNode::recalculatePorts() {
    distributePortsInRow(wires_);
}

bool BusVisualNode::handlePortSwap(const std::string& port_a,
                                  const std::string& port_b) {
    // Both must be alias ports (non-empty wire IDs)
    if (port_a.empty() || port_b.empty())
        return false;

    return swapAliasPorts(port_a, port_b);
}

bool BusVisualNode::swapAliasPorts(const std::string& wire_id_a,
                                   const std::string& wire_id_b) {
    // Find indices in wires_ vector
    size_t idx_a = SIZE_MAX, idx_b = SIZE_MAX;
    for (size_t i = 0; i < wires_.size(); i++) {
        if (wires_[i].id == wire_id_a) idx_a = i;
        if (wires_[i].id == wire_id_b) idx_b = i;
    }

    // Validation: both must exist and be different
    if (idx_a == SIZE_MAX || idx_b == SIZE_MAX || idx_a == idx_b)
        return false;

    // Swap in wires_ vector (this is the authoritative ordering!)
    std::swap(wires_[idx_a], wires_[idx_b]);

    // Redistribute ports to reflect new ordering
    distributePortsInRow(wires_);

    // Update node size if port count changed
    size_ = calculateBusSize(ports_.size());

    return true;
}

void BusVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                          bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);
    Pt screen_center((screen_min.x + screen_max.x) / 2,
                     (screen_min.y + screen_max.y) / 2);

    float bus_w = size_.x * vp.zoom;
    float bus_h = size_.y * vp.zoom;
    Pt bus_min(screen_center.x - bus_w / 2, screen_center.y - bus_h / 2);
    Pt bus_max(screen_center.x + bus_w / 2, screen_center.y + bus_h / 2);

    uint32_t fill = custom_color_.has_value()
        ? custom_color_->to_uint32()
        : render_theme::COLOR_BUS_FILL;
    float rounding = editor_constants::NODE_ROUNDING * vp.zoom;
    dl->add_rect_filled_with_rounding(bus_min, bus_max, fill, rounding);

    Pt text_pos(bus_min.x + 3 * vp.zoom, screen_center.y - 5 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, 10.0f * vp.zoom);

    // Border (render before ports)
    uint32_t border_color = is_selected ? render_theme::COLOR_SELECTED : render_theme::COLOR_BUS_BORDER;
    dl->add_rect_with_rounding_corners(bus_min, bus_max, border_color, rounding, editor_constants::DRAW_CORNERS_ALL, 1.0f);

    // Ports (render LAST for highest z-index, on top of border)
    float port_radius = editor_constants::PORT_RADIUS * vp.zoom;
    for (const auto& port : ports_) {
        Pt screen_pos = vp.world_to_screen(port.worldPosition(), canvas_min);
        uint32_t port_color = render_theme::get_port_color(port.type());
        dl->add_circle_filled(screen_pos, port_radius, port_color, 8);
    }
}

// ============================================================================
// RefVisualNode
// ============================================================================

RefVisualNode::RefVisualNode(const Node& node)
    : VisualNode(node)
{
    // Reset size — base constructor may have auto-sized for layout that Ref doesn't use
    size_ = snap_size_to_grid(node.size);
    // Override base class ports — Ref has a single port on top
    ports_.clear();

    // [c5a9b7d2] Use actual port name from node definition
    std::string port_name = "v";
    an24::PortType port_type = an24::PortType::V;
    if (!node.outputs.empty()) {
        port_name = node.outputs[0].name;
        port_type = node.outputs[0].type;
    } else if (!node.inputs.empty()) {
        port_name = node.inputs[0].name;
        port_type = node.inputs[0].type;
    }

    VisualPort vp(port_name, PortSide::Output, port_type);
    // [d5e6f7g8] Snap port position to grid so wires align to grid lines.
    vp.setWorldPosition(snap_to_grid(Pt(position_.x + size_.x / 2, position_.y)));
    ports_.push_back(std::move(vp));
}

void RefVisualNode::recalculatePorts() {
    if (!ports_.empty()) {
        // [d5e6f7g8] Keep port position grid-snapped after moves.
        ports_[0].setWorldPosition(snap_to_grid(Pt(position_.x + size_.x / 2, position_.y)));
    }
}

void RefVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                          bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);
    Pt screen_center((screen_min.x + screen_max.x) / 2,
                     (screen_min.y + screen_max.y) / 2);

    uint32_t fill = custom_color_.has_value()
        ? custom_color_->to_uint32()
        : render_theme::COLOR_BUS_FILL;
    float rounding = editor_constants::NODE_ROUNDING * vp.zoom;
    dl->add_rect_filled_with_rounding(screen_min, screen_max, fill, rounding);

    Pt text_pos(screen_min.x + 2 * vp.zoom, screen_center.y - 5 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, 10.0f * vp.zoom);

    // Border (render before port)
    uint32_t border_color = is_selected ? render_theme::COLOR_SELECTED : render_theme::COLOR_BUS_BORDER;
    dl->add_rect_with_rounding_corners(screen_min, screen_max, border_color, rounding, editor_constants::DRAW_CORNERS_ALL, 1.0f);

    // Port (render LAST for highest z-index, on top of border)
    // [d5e6f7g8] Use grid-snapped position so rendering matches port world position.
    Pt world_port_pos = snap_to_grid(Pt(position_.x + size_.x / 2, position_.y));
    Pt port_pos = vp.world_to_screen(world_port_pos, canvas_min);
    uint32_t port_color = render_theme::get_port_color(ports_[0].type());
    dl->add_circle_filled(port_pos, editor_constants::PORT_RADIUS * vp.zoom, port_color, 8);
}

// ============================================================================
// GroupVisualNode
// ============================================================================

GroupVisualNode::GroupVisualNode(const Node& node)
    : VisualNode(node)
{
    // Group uses size directly from data model (no auto-size layout)
    size_ = snap_size_to_grid(node.size);
    ports_.clear();
    port_slots_.clear();
}

bool GroupVisualNode::containsPoint(Pt world_pos) const {
    float x0 = position_.x, y0 = position_.y;
    float x1 = x0 + size_.x, y1 = y0 + size_.y;

    // Outside the bounding rect entirely → miss
    if (world_pos.x < x0 || world_pos.x > x1 ||
        world_pos.y < y0 || world_pos.y > y1)
        return false;

    float m = editor_constants::GROUP_BORDER_HIT_MARGIN;

    // Title bar (top strip): padding + font height + padding
    float title_h = editor_constants::GROUP_TITLE_PADDING * 2 + editor_constants::Font::Medium;
    if (world_pos.y <= y0 + title_h)
        return true;

    // Near any edge → hit (border area)
    if (world_pos.x <= x0 + m || world_pos.x >= x1 - m ||
        world_pos.y >= y1 - m)
        return true;

    // Interior → pass through
    return false;
}

void GroupVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                             bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    float rounding = editor_constants::GROUP_ROUNDING * vp.zoom;

    uint32_t fill = custom_color_.has_value()
        ? (custom_color_->to_uint32() & 0x00FFFFFF) | 0x30000000  // Force semi-transparent
        : render_theme::COLOR_GROUP_FILL;
    dl->add_rect_filled_with_rounding(screen_min, screen_max, fill, rounding);

    // Border only when selected
    if (is_selected) {
        dl->add_rect_with_rounding_corners(screen_min, screen_max,
                                           render_theme::COLOR_SELECTED, rounding,
                                           editor_constants::DRAW_CORNERS_ALL, 1.0f);
    }

    // Title text at top-left
    if (!name_.empty()) {
        float pad = editor_constants::GROUP_TITLE_PADDING * vp.zoom;
        Pt text_pos(screen_min.x + pad, screen_min.y + pad);
        dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_GROUP_TITLE,
                     editor_constants::Font::Medium * vp.zoom);
    }
}

// ============================================================================
// TextVisualNode
// ============================================================================

TextVisualNode::TextVisualNode(const Node& node)
    : VisualNode(node)
{
    size_ = snap_size_to_grid(node.size);
    ports_.clear();
    port_slots_.clear();

    auto it = node.params.find("text");
    if (it != node.params.end())
        text_ = it->second;

    // Parse font_size param: "small", "medium", "large" (default large)
    font_size_base_ = editor_constants::Font::Large;
    auto fs = node.params.find("font_size");
    if (fs != node.params.end()) {
        if (fs->second == "small")       font_size_base_ = editor_constants::Font::Small;
        else if (fs->second == "medium") font_size_base_ = editor_constants::Font::Medium;
    }
}

void TextVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                            bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    float rounding = editor_constants::GROUP_ROUNDING * vp.zoom;

    // Always show a faint border so the node is visible even when empty/unselected
    uint32_t border_color = is_selected
        ? render_theme::COLOR_SELECTED
        : render_theme::COLOR_TEXT_BORDER;
    dl->add_rect_with_rounding_corners(screen_min, screen_max, border_color, rounding,
                                       editor_constants::DRAW_CORNERS_ALL, 1.0f);

    float pad = editor_constants::GROUP_TITLE_PADDING * vp.zoom;
    float font_size = font_size_base_ * vp.zoom;

    if (text_.empty()) {
        // Placeholder text
        dl->add_text(Pt(screen_min.x + pad, screen_min.y + pad),
                     "Text", render_theme::COLOR_TEXT_DIM, font_size);
    } else {
        // Render multiline text with line breaks
        float line_height = font_size * 1.4f;
        float y = screen_min.y + pad;

        size_t pos = 0;
        while (pos < text_.size()) {
            size_t nl = text_.find('\n', pos);
            if (nl == std::string::npos) nl = text_.size();
            std::string line = text_.substr(pos, nl - pos);
            if (!line.empty()) {
                dl->add_text(Pt(screen_min.x + pad, y), line.c_str(),
                             render_theme::COLOR_TEXT, font_size);
            }
            y += line_height;
            pos = nl + 1;
        }
    }
}

// ============================================================================
// VisualNodeCache
// ============================================================================

VisualNode* VisualNodeCache::getOrCreate(const Node& node, const std::vector<Wire>& wires) {
    auto it = cache_.find(node.id);
    if (it == cache_.end()) {
        auto visual = VisualNodeFactory::create(node, wires);
        auto* ptr = visual.get();
        cache_[node.id] = std::move(visual);
        return ptr;
    }
    // Sync node_content from blueprint (simulation may have updated it)
    it->second->updateNodeContent(node.node_content);
    // Sync position from data model (loading/external edits may change it)
    it->second->setPosition(snap_to_grid(node.pos));
    // Sync size from data model for resizable nodes (Group/Text) where user
    // controls size. Auto-sized nodes compute their own size from layout.
    if (it->second->isResizable())
        it->second->setSize(node.size);
    return it->second.get();
}

VisualNode* VisualNodeCache::get(const std::string& node_id) {
    auto it = cache_.find(node_id);
    return it != cache_.end() ? it->second.get() : nullptr;
}

void VisualNodeCache::onWireAdded(const Wire& wire, const std::vector<Node>& all_nodes) {
    for (const auto& node : all_nodes) {
        if (node.id == wire.start.node_id || node.id == wire.end.node_id) {
            auto* visual = get(node.id);
            if (visual) {
                visual->connectWire(wire);
            }
        }
    }
}

void VisualNodeCache::onWireDeleted(const Wire& wire, const std::vector<Node>& all_nodes) {
    for (const auto& node : all_nodes) {
        if (node.id == wire.start.node_id || node.id == wire.end.node_id) {
            auto* visual = get(node.id);
            if (visual) {
                visual->disconnectWire(wire);
            }
        }
    }
}
