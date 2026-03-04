#include "visual_node.h"
#include "render.h"
#include <algorithm>
#include <cmath>

// ============================================================================
// Constants
// ============================================================================

namespace {

constexpr float HEADER_HEIGHT = 20.0f;
constexpr float PORT_RADIUS = 6.0f;
constexpr float BUS_SIZE = 40.0f;
constexpr float BUS_PORT_OFFSET = 12.0f;
constexpr float GRID_STEP = 16.0f;  // Grid unit for snapping

// Snap to grid helper
inline Pt snap_to_grid(Pt pos) {
    return Pt(
        std::round(pos.x / GRID_STEP) * GRID_STEP,
        std::round(pos.y / GRID_STEP) * GRID_STEP
    );
}

// Snap size to grid (ensure edges align with grid)
inline Pt snap_size_to_grid(Pt size) {
    return Pt(
        std::round(size.x / GRID_STEP) * GRID_STEP,
        std::round(size.y / GRID_STEP) * GRID_STEP
    );
}

// Colors (matching render.cpp)
constexpr uint32_t COLOR_TEXT = 0xFFFFFFFF;
constexpr uint32_t COLOR_TEXT_DIM = 0xFFAAAAAA;
constexpr uint32_t COLOR_BUS_FILL = 0xFF404060;
constexpr uint32_t COLOR_BUS_BORDER = 0xFF8080A0;
constexpr uint32_t COLOR_PORT_INPUT = 0xFFDCDCB4;
constexpr uint32_t COLOR_PORT_OUTPUT = 0xFFDCB4B4;
constexpr uint32_t COLOR_SELECTED = 0xFF00FF00;

} // anonymous namespace

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
{
    // Copy inputs from node
    for (size_t i = 0; i < node.inputs.size(); i++) {
        Port p;
        p.name = node.inputs[i].name;
        p.world_position = calculatePortPosition(i, PortSide::Input);
        inputs_.push_back(p);
    }

    // Copy outputs from node
    for (size_t i = 0; i < node.outputs.size(); i++) {
        Port p;
        p.name = node.outputs[i].name;
        p.world_position = calculatePortPosition(i, PortSide::Output);
        outputs_.push_back(p);
    }
}

Pt StandardVisualNode::calculatePortPosition(size_t index, PortSide side) const {
    size_t port_count = (side == PortSide::Input) ? inputs_.size() : outputs_.size();

    if (port_count == 0) {
        return snap_to_grid(Pt(position_.x + size_.x / 2, position_.y + size_.y / 2));
    }

    // Distance between ports = 1 grid unit
    float step = GRID_STEP;
    float y = position_.y + HEADER_HEIGHT + step * (index + 1);

    // Ports are ON the edge (not outside)
    float x;
    if (side == PortSide::Input) {
        x = position_.x; // Left edge
    } else {
        x = position_.x + size_.x; // Right edge
    }

    return snap_to_grid(Pt(x, y));
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
    if (index < inputs_.size()) {
        return &inputs_[index];
    }
    index -= inputs_.size();
    if (index < outputs_.size()) {
        return &outputs_[index];
    }
    return nullptr;
}

std::vector<std::string> StandardVisualNode::getPortNames() const {
    std::vector<std::string> names;
    for (const auto& p : inputs_) names.push_back(p.name);
    for (const auto& p : outputs_) names.push_back(p.name);
    return names;
}

Pt StandardVisualNode::getPortPosition(const std::string& port_name, const char* wire_id) const {
    (void)wire_id; // Not used for StandardVisualNode
    // Find port and calculate its position based on current node position
    for (size_t i = 0; i < inputs_.size(); i++) {
        if (inputs_[i].name == port_name) {
            return calculatePortPosition(i, PortSide::Input);
        }
    }
    for (size_t i = 0; i < outputs_.size(); i++) {
        if (outputs_[i].name == port_name) {
            return calculatePortPosition(i, PortSide::Output);
        }
    }
    return Pt(position_.x + size_.x / 2, position_.y + size_.y / 2);
}

void StandardVisualNode::connectWire(const Wire& wire) {
    // For StandardVisualNode, wires connect to existing ports
    // No dynamic port creation needed
    (void)wire;
}

void StandardVisualNode::disconnectWire(const Wire& wire) {
    // For StandardVisualNode, wires disconnect from existing ports
    (void)wire;
}

void StandardVisualNode::recalculatePorts() {
    // StandardVisualNode ports are fixed based on the original Node
    // No dynamic recalculation needed
}

void StandardVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                                bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    float header_height = HEADER_HEIGHT * vp.zoom;

    // Header background
    dl->add_rect_filled(screen_min,
        Pt(screen_max.x, screen_min.y + header_height), COLOR_BUS_FILL);

    // Body background
    dl->add_rect_filled(Pt(screen_min.x, screen_min.y + header_height),
        screen_max, 0xFF303040);

    // Border
    uint32_t border_color = is_selected ? COLOR_SELECTED : COLOR_BUS_BORDER;
    dl->add_rect(screen_min, screen_max, border_color, 1.0f);

    // Name in header
    Pt text_pos(screen_min.x + 5 * vp.zoom, screen_min.y + header_height / 2 - 6 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), COLOR_TEXT, 12.0f * vp.zoom);

    // Type name at bottom center (inside node body, not going outside)
    float type_font_size = 10.0f * vp.zoom;
    Pt type_size = dl->calc_text_size(type_name_.c_str(), type_font_size);
    // Bottom of body = screen_max.y - margin
    float body_bottom = screen_max.y - 5 * vp.zoom;
    Pt type_pos(screen_min.x + (screen_max.x - screen_min.x) / 2 - type_size.x / 2,
                body_bottom - type_font_size);
    dl->add_text(type_pos, type_name_.c_str(), COLOR_TEXT_DIM, type_font_size);

    // Input ports (left side) - port is ON the edge, label to the RIGHT of port
    for (size_t i = 0; i < inputs_.size(); i++) {
        Pt world_pos = calculatePortPosition(i, PortSide::Input);
        Pt screen_pos = vp.world_to_screen(world_pos, canvas_min);
        dl->add_circle_filled(screen_pos, PORT_RADIUS * vp.zoom, COLOR_PORT_INPUT, 8);

        // Port label - just right of the port circle, vertically centered
        float r = PORT_RADIUS * vp.zoom;
        Pt label_pos(screen_pos.x + r + 3 * vp.zoom, screen_pos.y - 5 * vp.zoom);
        dl->add_text(label_pos, inputs_[i].name.c_str(), COLOR_TEXT_DIM, 9.0f * vp.zoom);
    }

    // Output ports (right side) - port is ON the edge, label to the LEFT of port
    // Like Rust: Align2::RIGHT_CENTER means right edge of text at position
    for (size_t i = 0; i < outputs_.size(); i++) {
        Pt world_pos = calculatePortPosition(i, PortSide::Output);
        Pt screen_pos = vp.world_to_screen(world_pos, canvas_min);
        dl->add_circle_filled(screen_pos, PORT_RADIUS * vp.zoom, COLOR_PORT_OUTPUT, 8);

        // Port label - right edge at port left edge (like Rust Align2::RIGHT_CENTER)
        float r = PORT_RADIUS * vp.zoom;
        float font_size = 9.0f * vp.zoom;
        // Use calc_text_size to get exact width
        Pt text_size = dl->calc_text_size(outputs_[i].name.c_str(), font_size);
        Pt label_pos(screen_pos.x - r - 3 * vp.zoom - text_size.x, screen_pos.y - 5 * vp.zoom);
        dl->add_text(label_pos, outputs_[i].name.c_str(), COLOR_TEXT_DIM, font_size);
    }
}

// ============================================================================
// BusVisualNode
// ============================================================================

BusVisualNode::BusVisualNode(const Node& node, BusOrientation orientation, const std::vector<Wire>& wires)
    : BaseVisualNode(node)
    , orientation_(orientation)
    , name_(node.name)
    , wires_(wires)
{
    distributePortsInRow(wires);
}

void BusVisualNode::distributePortsInRow(const std::vector<Wire>& wires) {
    ports_.clear();

    // Count wires connected to this bus
    size_t port_count = 0;
    for (const auto& w : wires) {
        if (w.start.node_id == node_id_ || w.end.node_id == node_id_) {
            port_count++;
        }
    }

    // Bus nodes get size based on port count (dynamic)
    Pt new_size = calculateBusSize(port_count);
    size_ = new_size;

    // Create virtual ports: inout_0, inout_1, inout_2, ...
    for (size_t idx = 0; idx < port_count; idx++) {
        Port p;
        p.name = "inout_" + std::to_string(idx);
        p.world_position = calculatePortPosition(idx);
        ports_.push_back(p);
    }
}

Pt BusVisualNode::calculateBusSize(size_t port_count) const {
    float min_size = GRID_STEP * 2;  // At least 2 grid units

    // Use orientation to determine shape, but also consider actual width/height
    // If width > height, ports go on bottom (horizontal bus)
    // If height >= width, ports go on right (vertical bus)
    // For now use orientation

    Pt size;
    if (orientation_ == BusOrientation::Horizontal) {
        // Horizontal: width scales with port count, height is fixed
        // Width = (port_count + 2) * GRID_STEP for ports + margins
        size = Pt((port_count + 2) * GRID_STEP, GRID_STEP * 2);
    } else {
        // Vertical: width is fixed, height scales with port count
        size = Pt(GRID_STEP * 2, (port_count + 2) * GRID_STEP);
    }
    return snap_size_to_grid(size);
}

Pt BusVisualNode::calculatePortPosition(size_t index) const {
    size_t port_count = ports_.size();

    if (port_count == 0) {
        return snap_to_grid(Pt(position_.x + size_.x / 2, position_.y + size_.y / 2));
    }

    // If width > height: ports on bottom
    // If width <= height: ports on right
    bool ports_on_bottom = (size_.x > size_.y);

    // Distance between ports = 1 grid unit
    float step = GRID_STEP;

    if (ports_on_bottom) {
        // Ports on bottom edge (ON the edge, not outside)
        float x = position_.x + step * (index + 1);
        float y = position_.y + size_.y; // On bottom edge
        return snap_to_grid(Pt(x, y));
    } else {
        // Ports on right edge (ON the edge, not outside)
        float x = position_.x + size_.x; // On right edge
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
    if (index < ports_.size()) {
        return &ports_[index];
    }
    return nullptr;
}

std::vector<std::string> BusVisualNode::getPortNames() const {
    std::vector<std::string> names;
    for (const auto& p : ports_) {
        names.push_back(p.name);
    }
    return names;
}

Pt BusVisualNode::getPortPosition(const std::string& port_name, const char* wire_id) const {
    // First try direct lookup in ports_ (for inout_X names)
    for (size_t i = 0; i < ports_.size(); i++) {
        if (ports_[i].name == port_name) {
            return calculatePortPosition(i);
        }
    }

    // For port_name "v", use wire_id to find the correct port
    if (port_name == "v" && wire_id != nullptr) {
        // Find which wire in wires_ matches this wire_id
        size_t port_index = 0;
        for (const auto& w : wires_) {
            bool connects = (w.start.node_id == node_id_ || w.end.node_id == node_id_);
            if (!connects) continue;

            if (w.id == wire_id) {
                // Found the wire - return its port position
                return calculatePortPosition(port_index);
            }
            port_index++;
        }
    }

    // Fallback to center
    return Pt(position_.x + size_.x / 2, position_.y + size_.y / 2);
}

void BusVisualNode::connectWire(const Wire& wire) {
    // Check if wire connects to this bus
    if (wire.start.node_id == node_id_ || wire.end.node_id == node_id_) {
        // Check if this wire already has a port
        std::string port_name = wire.id;
        bool found = false;
        for (const auto& p : ports_) {
            if (p.name == port_name) {
                found = true;
                break;
            }
        }
        if (!found) {
            Port p;
            p.name = port_name;
            p.world_position = calculatePortPosition(ports_.size());
            ports_.push_back(p);

            // Recalculate size based on new port count
            size_ = calculateBusSize(ports_.size());

            // Recalculate all port positions
            for (size_t i = 0; i < ports_.size(); i++) {
                ports_[i].world_position = calculatePortPosition(i);
            }
        }
    }
}

void BusVisualNode::disconnectWire(const Wire& wire) {
    std::string port_name = wire.id;
    ports_.erase(
        std::remove_if(ports_.begin(), ports_.end(),
            [&](const Port& p) { return p.name == port_name; }),
        ports_.end());

    // Recalculate size based on new port count
    size_ = calculateBusSize(ports_.size());

    // Recalculate all port positions
    for (size_t i = 0; i < ports_.size(); i++) {
        ports_[i].world_position = calculatePortPosition(i);
    }
}

void BusVisualNode::recalculatePorts() {
    distributePortsInRow();
}

void BusVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                          bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    Pt screen_center((screen_min.x + screen_max.x) / 2,
                     (screen_min.y + screen_max.y) / 2);

    // Bus body - use dynamic size
    float bus_width = size_.x * vp.zoom;
    float bus_height = size_.y * vp.zoom;

    Pt bus_min(screen_center.x - bus_width / 2, screen_center.y - bus_height / 2);
    Pt bus_max(screen_center.x + bus_width / 2, screen_center.y + bus_height / 2);

    dl->add_rect_filled(bus_min, bus_max, COLOR_BUS_FILL);
    dl->add_rect(bus_min, bus_max, COLOR_BUS_BORDER, 1.0f);

    // Bus name
    Pt text_pos(bus_min.x + 3 * vp.zoom, screen_center.y - 5 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), COLOR_TEXT, 10.0f * vp.zoom);

    // Render ports along the side (recalculate positions each frame)
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
    // Ref nodes have a single port on top - snapped to grid
    Port p;
    p.name = "ref";
    p.world_position = snap_to_grid(Pt(position_.x + size_.x / 2, position_.y - PORT_RADIUS));
    ports_.push_back(p);
}

const BaseVisualNode::Port* RefVisualNode::getPort(const std::string& name) const {
    for (const auto& p : ports_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const BaseVisualNode::Port* RefVisualNode::getPort(size_t index) const {
    if (index < ports_.size()) {
        return &ports_[index];
    }
    return nullptr;
}

std::vector<std::string> RefVisualNode::getPortNames() const {
    std::vector<std::string> names;
    for (const auto& p : ports_) {
        names.push_back(p.name);
    }
    return names;
}

Pt RefVisualNode::getPortPosition(const std::string& port_name, const char* wire_id) const {
    (void)wire_id; // Not used for RefVisualNode
    // Ref nodes always have a single "ref" port on TOP edge - snapped to grid
    return snap_to_grid(Pt(position_.x + size_.x / 2, position_.y));
}

void RefVisualNode::connectWire(const Wire& wire) {
    (void)wire;
}

void RefVisualNode::disconnectWire(const Wire& wire) {
    (void)wire;
}

void RefVisualNode::recalculatePorts() {
    // Ref node has fixed single port
    if (!ports_.empty()) {
        ports_[0].world_position = Pt(position_.x + size_.x / 2, position_.y - PORT_RADIUS);
    }
}

void RefVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                          bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    Pt screen_center((screen_min.x + screen_max.x) / 2,
                     (screen_min.y + screen_max.y) / 2);

    // Ref node body (small rectangle)
    dl->add_rect_filled(screen_min, screen_max, COLOR_BUS_FILL);
    uint32_t border_color = is_selected ? COLOR_SELECTED : COLOR_BUS_BORDER;
    dl->add_rect(screen_min, screen_max, border_color, 1.0f);

    // Ref name
    Pt text_pos(screen_min.x + 2 * vp.zoom, screen_center.y - 5 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), COLOR_TEXT, 10.0f * vp.zoom);

    // Port on top (recalculate position each frame)
    Pt world_port_pos = Pt(position_.x + size_.x / 2, position_.y - PORT_RADIUS);
    Pt port_pos = vp.world_to_screen(world_port_pos, canvas_min);
    dl->add_circle_filled(port_pos, PORT_RADIUS * vp.zoom, COLOR_PORT_OUTPUT, 8);
}

// ============================================================================
// VisualNodeCache
// ============================================================================

BaseVisualNode* VisualNodeCache::getOrCreate(const Node& node) {
    auto it = cache_.find(node.id);
    if (it == cache_.end()) {
        auto visual = VisualNodeFactory::create(node);
        auto* ptr = visual.get();
        cache_[node.id] = std::move(visual);
        return ptr;
    }
    return it->second.get();
}

BaseVisualNode* VisualNodeCache::get(const std::string& node_id) {
    auto it = cache_.find(node_id);
    if (it != cache_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void VisualNodeCache::onWireAdded(const Wire& wire, const std::vector<Node>& all_nodes) {
    // Find and update start node
    for (const auto& node : all_nodes) {
        if (node.id == wire.start.node_id) {
            auto* visual = getOrCreate(node);
            visual->connectWire(wire);
            break;
        }
    }

    // Find and update end node
    for (const auto& node : all_nodes) {
        if (node.id == wire.end.node_id) {
            auto* visual = getOrCreate(node);
            visual->connectWire(wire);
            break;
        }
    }
}

void VisualNodeCache::onWireDeleted(const Wire& wire, const std::vector<Node>& all_nodes) {
    // Find and update start node
    for (const auto& node : all_nodes) {
        if (node.id == wire.start.node_id) {
            auto* visual = get(node.id);
            if (visual) {
                visual->disconnectWire(wire);
            }
            break;
        }
    }

    // Find and update end node
    for (const auto& node : all_nodes) {
        if (node.id == wire.end.node_id) {
            auto* visual = get(node.id);
            if (visual) {
                visual->disconnectWire(wire);
            }
            break;
        }
    }
}

Node* VisualNodeCache::findNode(const std::string& node_id, std::vector<Node>& nodes) {
    for (auto& node : nodes) {
        if (node.id == node_id) {
            return &node;
        }
    }
    return nullptr;
}
