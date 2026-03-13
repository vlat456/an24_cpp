#include "bus_node_widget.h"
#include "visual/render_context.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/handle_renderer.h"
#include "visual/wire/wire_end.h"
#include "editor/layout_constants.h"
#include "data/node.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace visual {

// ============================================================================
// Construction
// ============================================================================

BusNodeWidget::BusNodeWidget(const ::Node& data, BusOrientation orientation,
                             const std::vector<::Wire>& wires)
    : node_id_(data.id)
    , name_(data.name)
    , type_name_(data.type_name)
    , orientation_(orientation)
{
    if (data.color.has_value()) {
        custom_fill_ = data.color->to_uint32();
    }

    setLocalPos(data.pos);

    // Collect wires connected to this node
    for (const auto& w : wires) {
        if (w.start.node_id == node_id_ || w.end.node_id == node_id_) {
            wires_.push_back(w);
        }
    }

    // Initial size from data (will be recalculated in rebuildPorts)
    auto snap = [](float v) {
        constexpr float g = editor_constants::PORT_LAYOUT_GRID;
        return std::ceil(v / g) * g;
    };
    setSize(Pt(snap(data.size.x), snap(data.size.y)));

    rebuildPorts();
}

// ============================================================================
// Port management
// ============================================================================

void BusNodeWidget::rebuildPorts() {
    // Clear wire back-pointers on all WireEnd children BEFORE destroying
    // ports.  Without this, destroying a Port destroys its WireEnd children,
    // whose destructors call Wire::onEndpointDestroyed → scene()->remove(wire),
    // cascading into the removal of ALL visual wires on this bus node.
    for (auto* port : ports_) {
        for (auto& child : port->children()) {
            if (auto* we = dynamic_cast<WireEnd*>(child.get()))
                we->clearWire();
        }
    }
    ports_.clear();

    // Remove all children (ports are the only children of a bus node)
    while (!children().empty()) {
        removeChild(children().back().get());
    }

    // Create one alias port per connected wire
    for (const auto& w : wires_) {
        auto* p = emplaceChild<Port>(w.id, PortSide::InOut, PortType::V);
        ports_.push_back(p);
    }

    // Base "v" port (always present)
    auto* base = emplaceChild<Port>("v", PortSide::InOut, PortType::V);
    ports_.push_back(base);

    // Recalculate size based on port count
    if (!wires_.empty()) {
        setSize(calculateBusSize(ports_.size()));
    }

    // Position all ports
    for (size_t i = 0; i < ports_.size(); i++) {
        ports_[i]->setLocalPos(calculatePortLocalPos(i));
    }
}

Pt BusNodeWidget::calculateBusSize(size_t port_count) const {
    constexpr float g = editor_constants::PORT_LAYOUT_GRID;
    Pt sz;
    if (orientation_ == BusOrientation::Horizontal) {
        sz = Pt((port_count + 2) * g, g * 2);
    } else {
        sz = Pt(g * 2, (port_count + 2) * g);
    }
    // Snap to grid
    sz.x = std::ceil(sz.x / g) * g;
    sz.y = std::ceil(sz.y / g) * g;
    return sz;
}

Pt BusNodeWidget::calculatePortLocalPos(size_t index) const {
    if (ports_.empty() && index == 0) {
        return Pt(size().x / 2.0f - editor_constants::PORT_RADIUS,
                  size().y / 2.0f - editor_constants::PORT_RADIUS);
    }

    // Determine port edge from aspect ratio
    bool ports_on_bottom = (size().x > size().y);
    float step = editor_constants::PORT_LAYOUT_GRID;

    if (ports_on_bottom) {
        // Ports along bottom edge — offset so circle center sits on the edge
        float x = step * (index + 1) - editor_constants::PORT_RADIUS;
        float y = size().y - editor_constants::PORT_RADIUS;
        return Pt(x, y);
    } else {
        // Ports along right edge — offset so circle center sits on the edge
        float x = size().x - editor_constants::PORT_RADIUS;
        float y = step * (index + 1) - editor_constants::PORT_RADIUS;
        return Pt(x, y);
    }
}

Port* BusNodeWidget::resolveWirePort(const std::string& port_name,
                                      const std::string& wire_id) const {
    // If asking for "v" with a specific wire_id, return the alias port
    if (port_name == "v" && !wire_id.empty()) {
        for (auto* p : ports_) {
            if (p->name() == wire_id) return p;
        }
    }
    return port(port_name);
}

Port* BusNodeWidget::port(const std::string& name) const {
    for (auto* p : ports_) {
        if (p->name() == name) return p;
    }
    return nullptr;
}

Port* BusNodeWidget::portByName(std::string_view port_name,
                                std::string_view wire_id) const {
    // Bus nodes map wire_id to alias ports when port_name is "v"
    if (port_name == "v" && !wire_id.empty()) {
        for (auto* p : ports_) {
            if (p->name() == wire_id) return p;
        }
    }
    for (auto* p : ports_) {
        if (p->name() == port_name) return p;
    }
    return nullptr;
}

void BusNodeWidget::connectWire(const ::Wire& wire) {
    if (wire.start.node_id == node_id_ || wire.end.node_id == node_id_) {
        wires_.push_back(wire);
        rebuildPorts();
    }
}

void BusNodeWidget::disconnectWire(const ::Wire& wire) {
    wires_.erase(
        std::remove_if(wires_.begin(), wires_.end(),
            [&](const ::Wire& w) { return w.id == wire.id; }),
        wires_.end());
    rebuildPorts();
}

bool BusNodeWidget::swapAliasPorts(const std::string& wire_id_a,
                                    const std::string& wire_id_b) {
    size_t idx_a = SIZE_MAX, idx_b = SIZE_MAX;
    for (size_t i = 0; i < wires_.size(); i++) {
        if (wires_[i].id == wire_id_a) idx_a = i;
        if (wires_[i].id == wire_id_b) idx_b = i;
    }

    if (idx_a == SIZE_MAX || idx_b == SIZE_MAX || idx_a == idx_b)
        return false;

    std::swap(wires_[idx_a], wires_[idx_b]);
    rebuildPorts();
    return true;
}

// ============================================================================
// Layout
// ============================================================================

Pt BusNodeWidget::preferredSize(IDrawList* /*dl*/) const {
    return calculateBusSize(ports_.size());
}

void BusNodeWidget::layout(float w, float h) {
    setSize(Pt(w, h));
    for (size_t i = 0; i < ports_.size(); i++) {
        ports_[i]->setLocalPos(calculatePortLocalPos(i));
    }
}

// ============================================================================
// Rendering
// ============================================================================

void BusNodeWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = worldPos();
    Pt sz = size();
    float zoom = ctx.zoom;

    Pt screen_min = ctx.world_to_screen(pos);
    Pt screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float rounding = editor_constants::NODE_ROUNDING * zoom;

    // Body fill
    uint32_t fill = custom_fill_.value_or(render_theme::COLOR_BUS_FILL);
    dl->add_rect_filled_with_rounding(screen_min, screen_max, fill, rounding);

    // Name text
    Pt center((screen_min.x + screen_max.x) / 2.0f,
              (screen_min.y + screen_max.y) / 2.0f);
    Pt text_pos(screen_min.x + 3.0f * zoom, center.y - 5.0f * zoom);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, 10.0f * zoom);

    // Border
    dl->add_rect_with_rounding_corners(screen_min, screen_max,
                                       render_theme::COLOR_BUS_BORDER, rounding,
                                       editor_constants::DRAW_CORNERS_ALL, 1.0f);

}

void BusNodeWidget::renderPost(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = worldPos();
    Pt sz = size();
    Pt screen_min = ctx.world_to_screen(pos);
    Pt screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float rounding = editor_constants::NODE_ROUNDING * ctx.zoom;

    // Selection border drawn after children so it appears on top
    handle_renderer::draw_selection_border(*dl, ctx, *this, screen_min, screen_max, rounding);
}

} // namespace visual
