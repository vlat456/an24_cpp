# Dynamic Bus Node Ports - Implementation Design

## Overview

Bus nodes need dynamic ports that:
1. **Count**: Number of ports = number of connected wires
2. **Position**: Ports distributed around bus perimeter
3. **Real-time**: Ports update when wires added/removed

## Problem with Current Implementation

```cpp
// Current: all ports at center - wires overlap
if (node.kind == NodeKind::Bus) {
    return Pt(node.pos.x + node.size.x / 2, node.pos.y + node.size.y / 2);
}
```

## Solution Architecture

### 1. BusVisualNode Class

```cpp
class BusVisualNode : public BaseVisualNode {
public:
    BusVisualNode(const Node& node, const std::vector<Wire>& all_wires);

    // Recalculate ports when wires change
    void recalculatePorts(const std::vector<Wire>& all_wires);

    Pt getPortPosition(const std::string& port_name) const override;
    std::vector<std::string> getPortNames() const override;
    size_t getPortCount() const override { return port_positions_.size(); }

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                bool is_selected) const override;

private:
    // Port positions: port_name (wire_id) -> world position
    std::unordered_map<std::string, Pt> port_positions_;

    // Calculate positions around perimeter
    std::vector<Pt> distributePortsAroundPerimeter(size_t count) const;
};
```

### 2. Wire Connection Detection

```cpp
std::vector<WireConnection> getBusConnections(
    const std::string& bus_id,
    const std::vector<Wire>& wires) {

    std::vector<WireConnection> connections;

    for (const auto& w : wires) {
        if (w.start.node_id == bus_id) {
            connections.push_back({
                .wire_id = w.id,
                .port_name = w.start.port_name,
                .side = PortSide::Output
            });
        }
        if (w.end.node_id == bus_id) {
            connections.push_back({
                .wire_id = w.id,
                .port_name = w.end.port_name,
                .side = PortSide::Input
            });
        }
    }

    return connections;
}
```

### 3. Port Distribution Algorithm

#### Option A: Circular Distribution (Recommended)
Ports on a circle around the bus center.

```cpp
std::vector<Pt> distributePortsAroundPerimeter(size_t count) const {
    if (count == 0) return {};

    std::vector<Pt> positions;
    float center_x = position_.x + size_.x / 2;
    float center_y = position_.y + size_.y / 2;
    float radius = size_.x / 2 + PORT_OFFSET;  // outside the bus

    for (size_t i = 0; i < count; i++) {
        // Start from top, go clockwise
        float angle = -M_PI_2 + (2 * M_PI * i / count);
        float x = center_x + radius * std::cos(angle);
        float y = center_y + radius * std::sin(angle);
        positions.push_back(Pt(x, y));
    }

    return positions;
}
```

Visual:
```
        ○ port 1
        │
    ╭───┼───╮
   ─│   │   │─
    ╰───┼───╯
        │
        ○ port 2
```

#### Option B: Edge Distribution
Ports distributed on bus edges (like regular nodes but all sides).

```cpp
std::vector<Pt> distributePortsOnEdges(size_t count) const {
    // 4 edges: top, right, bottom, left
    // Distribute ports evenly across all edges
}
```

### 4. Port Naming

Each wire connected to Bus gets a unique port identifier:

```cpp
struct WireConnection {
    std::string wire_id;    // unique wire ID as port name
    PortSide side;          // input or output
};
```

**Port lookup**:
```cpp
Pt BusVisualNode::getPortPosition(const std::string& port_name) const {
    auto it = port_positions_.find(port_name);
    if (it != port_positions_.end()) {
        return it->second;
    }
    // Fallback to center if not found
    return Pt(position_.x + size_.x / 2, position_.y + size_.y / 2);
}
```

### 5. Real-time Update Trigger

When wires change, recalculate Bus ports:

```cpp
// In EditorApp
void onWireAdded(const Wire& w) {
    // Find Bus nodes connected to this wire
    if (isBusNode(w.start.node_id) || isBusNode(w.end.node_id)) {
        recalculateAllBusPorts();
    }
}

void onWireDeleted(const Wire& w) {
    if (isBusNode(w.start.node_id) || isBusNode(w.end.node_id)) {
        recalculateAllBusPorts();
    }
}

void recalculateAllBusPorts() {
    for (auto& n : blueprint.nodes) {
        if (n.kind == NodeKind::Bus) {
            // Update cached port positions
            n.bus_port_positions = calculateBusPortPositions(n, blueprint.wires);
        }
    }
}
```

### 6. Data Storage

Option A: Calculate on render (simple, no storage)
```cpp
// In render.cpp
auto visual = VisualNodeFactory::create(node, wires);  // passes wires
visual->render(...);
```

Option B: Cache in Node (faster rendering)
```cpp
// In node.h - add cached port positions
struct Node {
    ...
    // For Bus nodes: cached port positions
    std::unordered_map<std::string, Pt> bus_port_positions;
};
```

**Recommendation**: Option A for simplicity, optimize later if needed.

## Rendering Implementation

```cpp
void BusVisualNode::render(IDrawList* dl, const Viewport& vp,
                          Pt canvas_min, bool is_selected) const {
    // Render bus body (square)
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    float size = 30.0f * vp.zoom;
    Pt center(screen_min.x + size, screen_min.y + size);
    Pt bus_min(center.x - size, center.y - size);
    Pt bus_max(center.x + size, center.y + size);

    dl->add_rect_filled(bus_min, bus_max, COLOR_BUS_FILL);
    dl->add_rect(bus_min, bus_max, COLOR_BUS_BORDER, 1.0f);

    // Render ports around perimeter
    float port_radius = 6.0f * vp.zoom;
    for (const auto& [port_name, world_pos] : port_positions_) {
        Pt screen_pos = vp.world_to_screen(world_pos, canvas_min);
        dl->add_circle_filled(screen_pos, port_radius, COLOR_PORT, 8);
    }
}
```

## Integration Points

### render.cpp - Replace Bus rendering
```cpp
// OLD
if (n.kind == NodeKind::Bus) {
    // render all ports at center
}

// NEW
auto visual = VisualNodeFactory::create(n, blueprint.wires);
visual->render(dl, vp, canvas_min, is_selected);
```

### trigonometry.h - Replace get_port_position
```cpp
// OLD
if (node.kind == NodeKind::Bus) {
    return Pt(center);  // all ports at center
}

// NEW - delegate to visual node
auto visual = VisualNodeFactory::create(node, all_wires);
return visual->getPortPosition(port_name);
```

### app.cpp - Trigger recalculation
```cpp
// When wire is added/deleted
void updateBusPorts(Blueprint& bp) {
    for (auto& node : bp.nodes) {
        if (node.kind == NodeKind::Bus) {
            // Recalculate port positions based on current wires
            node.bus_port_positions = calculateBusPortPositions(node, bp.wires);
        }
    }
}
```

## Test Cases

1. **Bus with 0 wires**: No ports rendered
2. **Bus with 1 wire**: 1 port at top (angle = -π/2)
3. **Bus with 2 wires**: 2 ports at top and bottom
4. **Bus with 4 wires**: 4 ports at 0°, 90°, 180°, 270°
5. **Wire added**: New port appears at correct position
6. **Wire deleted**: Port disappears
7. **Zoom**: Ports stay at correct world positions

## Configuration

```cpp
// Constants
static constexpr float BUS_SIZE = 40.0f;           // visual size
static constexpr float BUS_PORT_OFFSET = 12.0f;    // distance from bus edge
static constexpr float BUS_PORT_RADIUS = 6.0f;     // port circle size
```

## Edge Cases

1. **Very large wire count** (>20): Consider pagination or edge distribution
2. **Wire to non-existent port**: Ignore, use auto-generated port
3. **Zoom**: World positions scale correctly via viewport transform
4. **Pan**: World positions move correctly via viewport transform

## Files to Modify

1. **New**: `src/editor/visual_node.h` - BaseVisualNode + BusVisualNode
2. **New**: `src/editor/visual_node.cpp` - implementations
3. **Mod**: `src/editor/render.cpp` - use VisualNodeFactory
4. **Mod**: `src/editor/trigonometry.h` - use VisualNodeFactory
5. **Mod**: `src/editor/app.cpp` - trigger recalculation on wire changes

## Future Enhancements

1. **Port labels**: connection info on hover Show wire ID or
2. **Drag to connect**: Drag from port edge to create new wire
3. **Edge distribution**: Switch to edge-based when port count > 8
4. **Animation**: Smooth transition when ports appear/disappear
