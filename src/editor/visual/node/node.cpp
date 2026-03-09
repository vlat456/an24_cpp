#include "visual/node/node.h"
#include "data/node.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "layout_constants.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>

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
    , size_(snap_size_to_grid(node.size))
    , node_id_(node.id)
    , ports_()
    , name_(node.name)
    , type_name_(node.type_name)
    , node_content_(node.node_content)
    , custom_color_(node.color)
{
    buildLayout(node);

    // Auto-size node to fit all widgets
    Pt preferred = layout_.getPreferredSize(nullptr);
    if (preferred.y > size_.y) {
        size_ = snap_size_to_grid(Pt(size_.x, preferred.y));
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
    // Header
    layout_.addWidget(std::make_unique<HeaderWidget>(name_, render_theme::COLOR_BUS_FILL));

    // Port rows: pair inputs and outputs
    size_t max_ports = std::max(node.inputs.size(), node.outputs.size());
    port_rows_.clear();
    for (size_t i = 0; i < max_ports; i++) {
        std::string left = (i < node.inputs.size()) ? node.inputs[i].name : "";
        std::string right = (i < node.outputs.size()) ? node.outputs[i].name : "";
        an24::PortType left_type = (i < node.inputs.size()) ? node.inputs[i].type : an24::PortType::Any;
        an24::PortType right_type = (i < node.outputs.size()) ? node.outputs[i].type : an24::PortType::Any;
        auto row = std::make_unique<PortRowWidget>(left, right, left_type, right_type);
        port_rows_.push_back(static_cast<PortRowWidget*>(
            layout_.addWidget(std::move(row))));
    }

    // Content area (flexible, takes remaining space)
    if (node_content_.type != NodeContentType::None) {
        if (node_content_.type == NodeContentType::Gauge) {
            layout_.addWidget(std::make_unique<VoltmeterWidget>(
                node_content_.value,
                node_content_.min,
                node_content_.max,
                node_content_.unit
            ));
        } else {
            float left_margin = editor_constants::PORT_RADIUS + 3.0f;
            float right_margin = editor_constants::PORT_RADIUS + 3.0f;
            for (const auto& p : node.inputs) {
                float lw = p.name.length() * 9.0f * 0.6f + editor_constants::PORT_RADIUS + 3.0f;
                left_margin = std::max(left_margin, lw);
            }
            for (const auto& p : node.outputs) {
                float lw = p.name.length() * 9.0f * 0.6f + editor_constants::PORT_RADIUS + 3.0f;
                right_margin = std::max(right_margin, lw);
            }
            layout_.addWidget(std::make_unique<ContentWidget>(
                node_content_.label, node_content_.value, left_margin, right_margin));
        }
    }

    // Type name at bottom
    layout_.addWidget(std::make_unique<TypeNameWidget>(type_name_));
}

void VisualNode::buildPorts(const Node& node) {
    ports_.clear();
    for (const auto& p : node.inputs) {
        VisualPort vp(p.name, PortSide::Input, p.type);
        for (const auto* row : port_rows_) {
            if (row->leftPortName() == p.name) {
                Pt local = row->leftPortCenter();
                vp.setWorldPosition(Pt(position_.x + local.x, position_.y + local.y));
                break;
            }
        }
        ports_.push_back(std::move(vp));
    }
    for (const auto& p : node.outputs) {
        VisualPort vp(p.name, PortSide::Output, p.type);
        for (const auto* row : port_rows_) {
            if (row->rightPortName() == p.name) {
                Pt local = row->rightPortCenter();
                vp.setWorldPosition(Pt(position_.x + local.x, position_.y + local.y));
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

void VisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                        bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    // BUGFIX [header-gap-fix] Use VISUAL_HEIGHT not HEIGHT to avoid gap
    // HeaderWidget reserves HEIGHT=24px but only renders VISUAL_HEIGHT=20px
    float header_visual_h = HeaderWidget::VISUAL_HEIGHT * vp.zoom;

    // Body background (below header) - use custom color if set
    uint32_t body_fill = custom_color_.has_value()
        ? custom_color_->to_uint32()
        : render_theme::COLOR_BODY_FILL;
    dl->add_rect_filled(Pt(screen_min.x, screen_min.y + header_visual_h), screen_max, body_fill);

    // Update VoltmeterWidget value before rendering
    if (node_content_.type == NodeContentType::Gauge) {
        for (size_t i = 0; i < layout_.childCount(); i++) {
            auto* w = layout_.child(i);
            if (auto* vw = dynamic_cast<VoltmeterWidget*>(w)) {
                vw->setValue(node_content_.value);
                break;
            }
        }
    }

    // Render all widgets (header, port rows, content, type name)
    layout_.render(dl, screen_min, vp.zoom);

    // Border (render LAST for highest z-index, so selection is visible above header)
    uint32_t border_color = is_selected ? render_theme::COLOR_SELECTED : render_theme::COLOR_BUS_BORDER;
    dl->add_rect(screen_min, screen_max, border_color, 1.0f);
}

Bounds VisualNode::getContentBounds() const {
    const ContentWidget* content_widget = nullptr;
    float max_left_edge = 0.0f;
    float max_right_edge = 0.0f;
    float content_height = 0.0f;
    bool found_any_port_row = false;

    for (size_t i = 0; i < layout_.childCount(); i++) {
        auto* w = layout_.child(i);
        if (auto* cw = dynamic_cast<const ContentWidget*>(w)) {
            content_widget = cw;
            content_height = cw->height();
        }
        if (auto* pr = dynamic_cast<const PortRowWidget*>(w)) {
            found_any_port_row = true;
            Bounds pb = pr->contentBounds();
            if (pb.x > max_left_edge) max_left_edge = pb.x;
            if (pb.x + pb.w > max_right_edge) max_right_edge = pb.x + pb.w;
        }
    }

    if (!content_widget) {
        return {};
    }

    if (found_any_port_row && max_right_edge > max_left_edge) {
        // [x8y9z0a1] Use symmetric margins for horizontal centering
        float left_margin  = max_left_edge;
        float right_margin = content_widget->width() - max_right_edge;
        float symmetric_margin = std::max(left_margin, right_margin);
        float content_w = content_widget->width() - 2.0f * symmetric_margin;

        return {
            content_widget->x() + symmetric_margin,
            content_widget->y(),
            std::max(0.0f, content_w),
            content_height
        };
    }

    Bounds content_area = content_widget->getContentArea();
    return {
        content_widget->x() + content_area.x,
        content_widget->y() + content_area.y,
        content_area.w,
        content_area.h
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
    dl->add_rect_filled(bus_min, bus_max, fill);

    Pt text_pos(bus_min.x + 3 * vp.zoom, screen_center.y - 5 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, 10.0f * vp.zoom);

    float port_radius = editor_constants::PORT_RADIUS * vp.zoom;
    for (const auto& port : ports_) {
        Pt screen_pos = vp.world_to_screen(port.worldPosition(), canvas_min);
        uint32_t port_color = render_theme::get_port_color(port.type());
        dl->add_circle_filled(screen_pos, port_radius, port_color, 8);
    }

    // Border (render LAST for highest z-index, so selection is visible above content)
    uint32_t border_color = is_selected ? render_theme::COLOR_SELECTED : render_theme::COLOR_BUS_BORDER;
    dl->add_rect(bus_min, bus_max, border_color, 1.0f);
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
    dl->add_rect_filled(screen_min, screen_max, fill);

    Pt text_pos(screen_min.x + 2 * vp.zoom, screen_center.y - 5 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, 10.0f * vp.zoom);

    // [d5e6f7g8] Use grid-snapped position so rendering matches port world position.
    Pt world_port_pos = snap_to_grid(Pt(position_.x + size_.x / 2, position_.y));
    Pt port_pos = vp.world_to_screen(world_port_pos, canvas_min);
    uint32_t port_color = render_theme::get_port_color(ports_[0].type());
    dl->add_circle_filled(port_pos, editor_constants::PORT_RADIUS * vp.zoom, port_color, 8);

    // Border (render LAST for highest z-index, so selection is visible above content)
    uint32_t border_color = is_selected ? render_theme::COLOR_SELECTED : render_theme::COLOR_BUS_BORDER;
    dl->add_rect(screen_min, screen_max, border_color, 1.0f);
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
