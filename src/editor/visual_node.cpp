#include "visual_node.h"
#include "data/node.h"
#include "render.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// ============================================================================
// Constants
// ============================================================================

namespace {

constexpr float PORT_RADIUS = 6.0f;
constexpr float GRID_STEP = 16.0f;

Pt snap_to_grid(Pt pos) {
    return Pt(
        std::round(pos.x / GRID_STEP) * GRID_STEP,
        std::round(pos.y / GRID_STEP) * GRID_STEP
    );
}

Pt snap_size_to_grid(Pt size) {
    return Pt(
        std::ceil(size.x / GRID_STEP) * GRID_STEP,
        std::ceil(size.y / GRID_STEP) * GRID_STEP
    );
}

// Node style colors
constexpr uint32_t COLOR_TEXT = 0xFFFFFFFF;
constexpr uint32_t COLOR_TEXT_DIM = 0xFFAAAAAA;
constexpr uint32_t COLOR_BUS_FILL = 0xFF404060;
constexpr uint32_t COLOR_BUS_BORDER = 0xFF8080A0;
constexpr uint32_t COLOR_BODY_FILL = 0xFF303040;
constexpr uint32_t COLOR_PORT_INPUT = 0xFFDCDCB4;
constexpr uint32_t COLOR_PORT_OUTPUT = 0xFFDCB4B4;
constexpr uint32_t COLOR_SELECTED = 0xFF00FF00;

}  // anonymous namespace

// ============================================================================
// BaseVisualNode
// ============================================================================

BaseVisualNode::BaseVisualNode(const Node& node)
    : position_(snap_to_grid(node.pos))
    , size_(snap_size_to_grid(node.size))
    , node_id_(node.id)
    , ports_()
{}

// ============================================================================
// StandardVisualNode
// ============================================================================

StandardVisualNode::StandardVisualNode(const Node& node)
    : BaseVisualNode(node)
    , inputs_()
    , outputs_()
    , name_(node.name)
    , type_name_(node.type_name)
    , node_content_(node.node_content)
{
    // Store port info
    for (const auto& p : node.inputs) {
        Port port;
        port.name = p.name;
        inputs_.push_back(port);
    }
    for (const auto& p : node.outputs) {
        Port port;
        port.name = p.name;
        outputs_.push_back(port);
    }

    // Build widget layout
    buildLayout();

    // Auto-size node to fit all widgets
    Pt preferred = layout_.getPreferredSize(nullptr);
    if (preferred.y > size_.y) {
        size_ = snap_size_to_grid(Pt(size_.x, preferred.y));
    }

    // Recalculate layout with final size
    layout_.layout(size_.x, size_.y);

    // Cache world positions in port structs
    for (size_t i = 0; i < inputs_.size(); i++) {
        inputs_[i].world_position = getPortPosition(inputs_[i].name);
    }
    for (size_t i = 0; i < outputs_.size(); i++) {
        outputs_[i].world_position = getPortPosition(outputs_[i].name);
    }
}

void StandardVisualNode::buildLayout() {
    // Header
    layout_.addWidget(std::make_unique<HeaderWidget>(name_, COLOR_BUS_FILL));

    // Port rows: pair inputs and outputs
    size_t max_ports = std::max(inputs_.size(), outputs_.size());
    port_rows_.clear();
    for (size_t i = 0; i < max_ports; i++) {
        std::string left = (i < inputs_.size()) ? inputs_[i].name : "";
        std::string right = (i < outputs_.size()) ? outputs_[i].name : "";
        auto row = std::make_unique<PortRowWidget>(left, right);
        port_rows_.push_back(static_cast<PortRowWidget*>(
            layout_.addWidget(std::move(row))));
    }

    // Content area (flexible, takes remaining space)
    if (node_content_.type != NodeContentType::None) {
        float left_margin = PORT_RADIUS + 3.0f;
        float right_margin = PORT_RADIUS + 3.0f;
        for (const auto& p : inputs_) {
            float lw = p.name.length() * 9.0f * 0.6f + PORT_RADIUS + 3.0f;
            left_margin = std::max(left_margin, lw);
        }
        for (const auto& p : outputs_) {
            float lw = p.name.length() * 9.0f * 0.6f + PORT_RADIUS + 3.0f;
            right_margin = std::max(right_margin, lw);
        }
        layout_.addWidget(std::make_unique<ContentWidget>(
            node_content_.label, node_content_.value, left_margin, right_margin));
    }

    // Type name at bottom
    layout_.addWidget(std::make_unique<TypeNameWidget>(type_name_));
}

size_t StandardVisualNode::getPortCount() const {
    return inputs_.size() + outputs_.size();
}

const BaseVisualNode::Port* StandardVisualNode::getPort(const std::string& name) const {
    for (const auto& p : inputs_) {
        if (p.name == name) return &p;
    }
    for (const auto& p : outputs_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const BaseVisualNode::Port* StandardVisualNode::getPort(size_t index) const {
    if (index < inputs_.size()) return &inputs_[index];
    index -= inputs_.size();
    if (index < outputs_.size()) return &outputs_[index];
    return nullptr;
}

std::vector<std::string> StandardVisualNode::getPortNames() const {
    std::vector<std::string> names;
    for (const auto& p : inputs_) names.push_back(p.name);
    for (const auto& p : outputs_) names.push_back(p.name);
    return names;
}

Pt StandardVisualNode::getPortPosition(const std::string& port_name,
                                       const char* wire_id) const {
    (void)wire_id;

    // Search port rows for matching port name
    for (const auto* row : port_rows_) {
        if (row->leftPortName() == port_name) {
            Pt local = row->leftPortCenter();
            return Pt(position_.x + local.x, position_.y + local.y);
        }
        if (row->rightPortName() == port_name) {
            Pt local = row->rightPortCenter();
            return Pt(position_.x + local.x, position_.y + local.y);
        }
    }

    // Fallback: center of node
    return Pt(position_.x + size_.x / 2, position_.y + size_.y / 2);
}

void StandardVisualNode::connectWire(const Wire&) {}
void StandardVisualNode::disconnectWire(const Wire&) {}
void StandardVisualNode::recalculatePorts() {}

void StandardVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                                bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    float header_h = HeaderWidget::HEIGHT * vp.zoom;

    // Body background (below header)
    dl->add_rect_filled(Pt(screen_min.x, screen_min.y + header_h), screen_max, COLOR_BODY_FILL);

    // Border
    uint32_t border_color = is_selected ? COLOR_SELECTED : COLOR_BUS_BORDER;
    dl->add_rect(screen_min, screen_max, border_color, 1.0f);

    // Render all widgets (header, port rows, content, type name)
    layout_.render(dl, screen_min, vp.zoom);
}

NodeContentType StandardVisualNode::getContentType() const {
    return node_content_.type;
}

const NodeContent& StandardVisualNode::getNodeContent() const {
    return node_content_;
}

Bounds StandardVisualNode::getContentBounds() const {
    // Find the ContentWidget and calculate proper content bounds from port rows
    const ContentWidget* content_widget = nullptr;
    float max_left_edge = 0.0f;
    float max_right_edge = 0.0f;
    float content_height = 0.0f;
    bool found_any_port_row = false;

    // First find the content widget and collect all port row bounds
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

    // Use port row bounds if available, otherwise use widget's own content area
    if (found_any_port_row && max_right_edge > max_left_edge) {
        Bounds result = {
            content_widget->x() + max_left_edge,
            content_widget->y(),
            std::max(0.0f, max_right_edge - max_left_edge),
            content_height
        };
        return result;
    }

    // Fallback: use widget's getContentArea() method
    Bounds content_area = content_widget->getContentArea();
    Bounds result = {
        content_widget->x() + content_area.x,
        content_widget->y() + content_area.y,
        content_area.w,
        content_area.h
    };
    return result;
}

// ============================================================================
// BusVisualNode
// ============================================================================

BusVisualNode::BusVisualNode(const Node& node, BusOrientation orientation,
                             const std::vector<Wire>& wires)
    : BaseVisualNode(node)
    , orientation_(orientation)
    , name_(node.name)
    , wires_(wires)
{
    // Always create at least the main "v" port so Bus can be connected
    Port v_port;
    v_port.name = "v";
    v_port.target_port = "";  // Empty means same as name (logical port)
    v_port.world_position = calculatePortPosition(0);
    ports_.push_back(v_port);
    size_ = calculateBusSize(ports_.size());

    // Then add ports for each connected wire
    distributePortsInRow(wires_);
}

void BusVisualNode::distributePortsInRow(const std::vector<Wire>& wires) {
    ports_.clear();

    // Always create the main "v" port first (even if no wires)
    Port v_port;
    v_port.name = "v";
    v_port.target_port = "";  // Empty means same as name (logical port)
    v_port.world_position = calculatePortPosition(0);
    ports_.push_back(v_port);

    // Add ports for each connected wire (named after wire ID)
    // These are visual aliases that target the logical port "v"
    for (const auto& w : wires) {
        if (w.start.node_id == node_id_ || w.end.node_id == node_id_) {
            Port p;
            p.name = w.id;              // Visual alias name (wire ID)
            p.target_port = "v";        // Targets logical port "v"
            p.world_position = calculatePortPosition(ports_.size());
            ports_.push_back(p);
        }
    }

    size_ = calculateBusSize(ports_.size());

    // Recalculate all port positions
    for (size_t i = 0; i < ports_.size(); i++) {
        ports_[i].world_position = calculatePortPosition(i);
    }
}

Pt BusVisualNode::calculateBusSize(size_t port_count) const {
    Pt size;
    if (orientation_ == BusOrientation::Horizontal) {
        size = Pt((port_count + 2) * GRID_STEP, GRID_STEP * 2);
    } else {
        size = Pt(GRID_STEP * 2, (port_count + 2) * GRID_STEP);
    }
    return snap_size_to_grid(size);
}

Pt BusVisualNode::calculatePortPosition(size_t index) const {
    if (ports_.empty() && index == 0) {
        return snap_to_grid(Pt(position_.x + size_.x / 2, position_.y + size_.y / 2));
    }

    bool ports_on_bottom = (size_.x > size_.y);
    float step = GRID_STEP;

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

const BaseVisualNode::Port* BusVisualNode::getPort(const std::string& name) const {
    for (const auto& p : ports_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const BaseVisualNode::Port* BusVisualNode::getPort(size_t index) const {
    return index < ports_.size() ? &ports_[index] : nullptr;
}

std::vector<std::string> BusVisualNode::getPortNames() const {
    std::vector<std::string> names;
    for (const auto& p : ports_) names.push_back(p.name);
    return names;
}

Pt BusVisualNode::getPortPosition(const std::string& port_name,
                                  const char* wire_id) const {
    // [e7b4c2d5] When wire_id is provided and port_name is "v", resolve by wire_id FIRST.
    // Previously the "v" port was found at index 0 before the wire_id fallback was reached,
    // causing all wire endpoints to snap to the main "v" port position.
    if (port_name == "v" && wire_id != nullptr) {
        // Wire alias ports start at index 1 (index 0 is the main "v" port).
        size_t port_index = 1;
        for (const auto& w : wires_) {
            bool connects = (w.start.node_id == node_id_ || w.end.node_id == node_id_);
            if (!connects) continue;
            if (w.id == wire_id) {
                return calculatePortPosition(port_index);
            }
            port_index++;
        }
    }

    for (size_t i = 0; i < ports_.size(); i++) {
        if (ports_[i].name == port_name) {
            return calculatePortPosition(i);
        }
    }

    return Pt(position_.x + size_.x / 2, position_.y + size_.y / 2);
}

void BusVisualNode::connectWire(const Wire& wire) {
    if (wire.start.node_id == node_id_ || wire.end.node_id == node_id_) {
        // [b8d2e4f1] Add wire to wires_ before redistributing;
        // otherwise distributePortsInRow uses stale wire list.
        wires_.push_back(wire);
        distributePortsInRow(wires_);
    }
}

void BusVisualNode::disconnectWire(const Wire& wire) {
    // [b8d2e4f1] Also remove wire from wires_ to keep it in sync
    wires_.erase(
        std::remove_if(wires_.begin(), wires_.end(),
            [&](const Wire& w) { return w.id == wire.id; }),
        wires_.end());
    std::string port_name = wire.id;
    ports_.erase(
        std::remove_if(ports_.begin(), ports_.end(),
            [&](const Port& p) { return p.name == port_name; }),
        ports_.end());
    size_ = calculateBusSize(ports_.size());
    for (size_t i = 0; i < ports_.size(); i++) {
        ports_[i].world_position = calculatePortPosition(i);
    }
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

    dl->add_rect_filled(bus_min, bus_max, COLOR_BUS_FILL);
    uint32_t border_color = is_selected ? COLOR_SELECTED : COLOR_BUS_BORDER;
    dl->add_rect(bus_min, bus_max, border_color, 1.0f);

    Pt text_pos(bus_min.x + 3 * vp.zoom, screen_center.y - 5 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), COLOR_TEXT, 10.0f * vp.zoom);

    float port_radius = PORT_RADIUS * vp.zoom;
    for (size_t i = 0; i < ports_.size(); i++) {
        Pt world_pos = calculatePortPosition(i);
        Pt screen_pos = vp.world_to_screen(world_pos, canvas_min);
        dl->add_circle_filled(screen_pos, port_radius, COLOR_PORT_INPUT, 8);
    }
}

// ============================================================================
// RefVisualNode
// ============================================================================

RefVisualNode::RefVisualNode(const Node& node)
    : BaseVisualNode(node)
    , name_(node.name)
{
    // [c5a9b7d2] Use actual port name from node definition instead of hardcoded "ref".
    // RefNode.json defines port "v" — hit test resolves port_name from visual port,
    // so using "ref" caused wire creation to use wrong port name.
    std::string port_name = "v";  // default fallback
    if (!node.outputs.empty()) {
        port_name = node.outputs[0].name;
    } else if (!node.inputs.empty()) {
        port_name = node.inputs[0].name;
    }
    Port p;
    p.name = port_name;
    p.world_position = Pt(position_.x + size_.x / 2, position_.y);
    ports_.push_back(p);
}

const BaseVisualNode::Port* RefVisualNode::getPort(const std::string& name) const {
    for (const auto& p : ports_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const BaseVisualNode::Port* RefVisualNode::getPort(size_t index) const {
    return index < ports_.size() ? &ports_[index] : nullptr;
}

std::vector<std::string> RefVisualNode::getPortNames() const {
    std::vector<std::string> names;
    for (const auto& p : ports_) names.push_back(p.name);
    return names;
}

Pt RefVisualNode::getPortPosition(const std::string&, const char*) const {
    return Pt(position_.x + size_.x / 2, position_.y);
}

void RefVisualNode::connectWire(const Wire&) {}
void RefVisualNode::disconnectWire(const Wire&) {}

void RefVisualNode::recalculatePorts() {
    if (!ports_.empty()) {
        ports_[0].world_position = Pt(position_.x + size_.x / 2, position_.y);
    }
}

void RefVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                          bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);
    Pt screen_center((screen_min.x + screen_max.x) / 2,
                     (screen_min.y + screen_max.y) / 2);

    dl->add_rect_filled(screen_min, screen_max, COLOR_BUS_FILL);
    uint32_t border_color = is_selected ? COLOR_SELECTED : COLOR_BUS_BORDER;
    dl->add_rect(screen_min, screen_max, border_color, 1.0f);

    Pt text_pos(screen_min.x + 2 * vp.zoom, screen_center.y - 5 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), COLOR_TEXT, 10.0f * vp.zoom);

    Pt world_port_pos = Pt(position_.x + size_.x / 2, position_.y);
    Pt port_pos = vp.world_to_screen(world_port_pos, canvas_min);
    dl->add_circle_filled(port_pos, PORT_RADIUS * vp.zoom, COLOR_PORT_OUTPUT, 8);
}

// ============================================================================
// VisualNodeCache
// ============================================================================

BaseVisualNode* VisualNodeCache::getOrCreate(const Node& node, const std::vector<Wire>& wires) {
    auto it = cache_.find(node.id);
    if (it == cache_.end()) {
        auto visual = VisualNodeFactory::create(node, wires);
        auto* ptr = visual.get();
        cache_[node.id] = std::move(visual);
        return ptr;
    }
    return it->second.get();
}

BaseVisualNode* VisualNodeCache::get(const std::string& node_id) {
    auto it = cache_.find(node_id);
    return it != cache_.end() ? it->second.get() : nullptr;
}

void VisualNodeCache::onWireAdded(const Wire& wire, const std::vector<Node>& all_nodes) {
    (void)wire;  // Will be used when iterating wires
    for (const auto& node : all_nodes) {
        if (node.id == wire.start.node_id || node.id == wire.end.node_id) {
            // For Bus nodes, remove from cache to force recreation with updated wire list
            // This is necessary because BusVisualNode creates dynamic ports based on ALL wires
            if (node.kind == NodeKind::Bus) {
                cache_.erase(node.id);
            } else {
                // For other nodes, use connectWire method
                auto* visual = get(node.id);
                if (visual) {
                    visual->connectWire(wire);
                }
            }
        }
    }
}

void VisualNodeCache::onWireDeleted(const Wire& wire, const std::vector<Node>& all_nodes) {
    (void)wire;  // Will be used when iterating wires
    for (const auto& node : all_nodes) {
        if (node.id == wire.start.node_id || node.id == wire.end.node_id) {
            // For Bus nodes, remove from cache to force recreation with updated wire list
            if (node.kind == NodeKind::Bus) {
                cache_.erase(node.id);
            } else {
                // For other nodes, use disconnectWire method
                auto* visual = get(node.id);
                if (visual) {
                    visual->disconnectWire(wire);
                }
            }
        }
    }
}
