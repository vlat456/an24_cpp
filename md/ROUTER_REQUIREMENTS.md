# Wire Router Requirements

## Current Status (Before Fix)

### Working Features:
- A* pathfinding on grid
- Turn penalty (avoid staircase)
- Snap-to-grid for nodes and routing points
- Wire crossing detection with arcs
- Visual feedback (selected = gold, unselected = gray)

### Broken/Not Implemented:
- **Node padding**: Wires should route 1 grid cell away from nodes
- **Wire padding**: Wires should route 1 grid cell away from other wires
- **Arc direction**: Arc should follow wire direction (horizontal/vertical)
- **Jump over**: Ability to route over perpendicular wires (cost-based)

## Requirements

### 1. Node Padding
- All nodes have a rectangular bounding box
- Wire path must stay at least 1 grid cell away from node edges
- This is the "clearance" or "padding" zone
- Implementation: Expand node rect by 1 grid cell when building obstacle map

### 2. Wire Padding
- Wires should not overlap
- Maintain 1 grid cell distance between parallel wires
- Similar to node padding - add to obstacle map

### 3. Arc Direction (Jump Over Visualization)
- When two wires cross, draw a semicircular arc
- Arc direction = perpendicular to wire direction:
  - Horizontal wire → vertical arc (goes up/down)
  - Vertical wire → horizontal arc (goes left/right)
- Only higher-index wire draws arc (avoid duplicates)

### 4. Jump Over Logic (Future)
- Allow routing over perpendicular wires with extra cost
- Not blocking - just higher cost path
- Visual arc indicates the jump

## Implementation Notes

### Grid Coordinate System
- World coords → Grid coords: round(x / grid_step)
- Grid coords → World coords: x * grid_step

### Obstacle Map
```cpp
struct Grid {
    int width, height;
    std::vector<uint8_t> cells;  // bit flags
};
```

### Cell Flags
- Empty = 0
- Node = 1 (with padding built-in)
- WireH = 2 (horizontal wire)
- WireV = 4 (vertical wire)

### Cost Function
- Base step: 1.0
- Turn penalty: 15.0 (high to avoid staircase)
- Jump over perpendicular wire: 5.0
