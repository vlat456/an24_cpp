# Dynamic Bus Node Ports Design

## Problem

Current Bus node implementation:
- All ports rendered at center position
- Single visual port regardless of wire count
- Wires route to center and overlap when multiple wires connect

## Goal

Bus node should have:
- **Dynamic port count**: number of visual ports = number of connected wires
- **Dynamic port positions**: distribute ports evenly around the Bus node
- **Real-time updates**: when wires added/removed, ports update automatically

## Current Implementation

### render.cpp (lines 287-307)
```cpp
if (n.kind == NodeKind::Bus) {
    // All ports at center
    for (size_t i = 0; i < n.inputs.size(); i++) {
        dl->add_circle_filled(screen_center, port_radius, ...);
    }
    for (size_t i = 0; i < n.outputs.size(); i++) {
        dl->add_circle_filled(screen_center, port_radius, ...);
    }
}
```

### trigonometry.h (lines 14-17)
```cpp
if (node.kind == NodeKind::Bus) {
    return Pt(node.pos.x + node.size.x / 2, node.pos.y + node.size.y / 2);
}
```

## Proposed Solution

### 1. Wire-Aware Port Calculation

Instead of using `node.inputs` and `node.outputs`, we need to count wires connected to this Bus.

```cpp
struct BusPortInfo {
    std::string wire_id;
    std::string port_name;  // e.g., "1", "2", "3"...
    Pt position;            // calculated position around bus perimeter
    PortSide side;          // input or output
};

/// Get all wire connections to a Bus node
std::vector<BusPortInfo> get_bus_connections(
    const Node& bus_node,
    const std::vector<Wire>& wires);
```

### 2. Port Distribution Algorithm

Ports should be distributed evenly around the Bus perimeter (not at center).

```cpp
/// Calculate port positions around Bus perimeter
/// - ports spaced evenly along circumference (for small bus)
/// - or distributed on edges (for larger bus)
std::vector<Pt> distribute_bus_ports(
    Pt bus_center,
    float bus_size,
    size_t port_count,
    float port_radius);
```

Distribution options:
1. **Circular** - ports on circle around center (for small number of ports)
2. **Edge-based** - ports distributed on bus edges (like regular nodes)

### 3. Updated render.cpp

```cpp
if (n.kind == NodeKind::Bus) {
    // Get wire connections for this bus
    auto connections = get_bus_connections(n, blueprint.wires);

    // Calculate port positions (distributed around perimeter)
    auto port_positions = distribute_bus_ports(
        screen_center, size, connections.size(), port_radius);

    // Render each port at calculated position
    for (size_t i = 0; i < connections.size(); i++) {
        bool is_output = (connections[i].side == PortSide::Output);
        uint32_t color = is_output ? COLOR_PORT_OUTPUT : COLOR_PORT_INPUT;
        dl->add_circle_filled(port_positions[i], port_radius, color, 8);
    }
}
```

### 4. Updated trigonometry.h

```cpp
if (node.kind == NodeKind::Bus) {
    // Need blueprint context to find wire connections
    // Or: pass pre-calculated port positions
}
```

**Alternative**: Store calculated port positions in Node, recalculate when wires change.

## Data Flow

1. **On wire add/remove**: trigger bus port recalculation
2. **Port position calculation**: needs Blueprint context (wires)
3. **Store in Node**: cache port positions for render
4. **Render**: use stored positions

## Implementation Steps

### Phase 1: Basic Dynamic Ports
1. Add helper function to count wires connected to Bus
2. Add port position calculation for Bus (circular distribution)
3. Update render.cpp to use dynamic positions
4. Update get_port_position() to return correct position by port_name

### Phase 2: Real-time Updates
1. Add callback/notification when wires change
2. Recalculate Bus ports when wire added/removed
3. Update persist to save/load dynamic port info

### Phase 3: Edge Distribution (Future)
- For many ports, distribute on edges instead of circle
- Like regular nodes but with more ports

## Wire Connection Detection

```cpp
std::vector<BusPortInfo> get_bus_connections(
    const Node& bus,
    const std::vector<Wire>& wires) {

    std::vector<BusPortInfo> connections;

    for (const auto& w : wires) {
        if (w.start.node_id == bus.id) {
            connections.push_back({
                w.id,
                w.start.port_name,
                {},  // position calculated later
                PortSide::Output
            });
        }
        if (w.end.node_id == bus.id) {
            connections.push_back({
                w.id,
                w.end.port_name,
                {},
                PortSide::Input
            });
        }
    }

    return connections;
}
```

## Port Naming

For Bus nodes, port names could be:
- Auto-generated: "1", "2", "3"...
- Or use actual wire IDs

Current: uses `port_name` from WireEnd

## Rendering

Visual style:
```
    ┌─────┐
    │     │ ← port 1 (top)
    │ BUS │
    │     │ ← port 2 (bottom)
    └─────┘
```

Or circular:
```
      ○ port 1
    ╱   ╲
   │ BUS │
    ╲   ╱
      ○ port 2
```

## Files to Modify

1. `src/editor/trigonometry.h` - add get_bus_connections(), distribute_bus_ports()
2. `src/editor/render.cpp` - update Bus rendering to use dynamic ports
3. `src/editor/data/node.h` - optionally add cached port positions
4. `src/editor/app.cpp` - trigger recalculation on wire changes

## Testing

1. Bus with 0 wires: no ports rendered
2. Bus with 1 wire: 1 port at correct position
3. Bus with 3 wires: 3 ports evenly distributed
4. Wire added to Bus: new port appears
5. Wire removed from Bus: port disappears
6. Zoom: ports stay correctly positioned
