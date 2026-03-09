# Task: Fix Bus Port Swap - Wire Reconnection Issue

## Context

This is a C++ flight simulation project with a node-based editor. We're implementing **bus port swapping** functionality where users can drag a wire end from one bus port to another (within the same bus), and the ports should physically swap places while keeping wires attached.

## Current Implementation (What's Done)

### Architecture
- **VisualNode** - Base class for visual node representation
- **BusVisualNode** - Bus node with dynamic ports (alias ports)
- **CanvasInput** - FSM-based input handling
- **VisualScene** - Scene graph managing nodes/wires
- **WireManager** - Wire routing with A* algorithm

### What Works
✅ Port swapping logic implemented:
```cpp
// visual/node/node.h
class VisualNode {
    virtual bool handlePortSwap(const std::string& port_a, const std::string& port_b) {
        return false;  // Default: no swapping
    }
};

class BusVisualNode : public VisualNode {
    bool handlePortSwap(const std::string& port_a, const std::string& port_b) override;
    bool swapAliasPorts(const std::string& wire_id_a, const std::string& wire_id_b);
private:
    std::vector<Wire> wires_;  // Visual copy of connected wires
    std::vector<VisualPort> ports_;  // Alias ports + logical "v" port
};
```

✅ Integration with CanvasInput:
```cpp
// input/canvas_input.cpp
InputResult CanvasInput::finish_wire_reconnection(Pt screen_pos, Pt canvas_min) {
    // ... existing code ...
    if (port_hit.port_node_id == detached.node_id) {
        if (scene_.swapWirePortsOnBus(port_hit.port_node_id,
                                     port_hit.port_wire_id, detached.port_name)) {
            reconnected = true;
            result.rebuild_simulation = true;
        }
    }
}
```

✅ Test coverage (7 tests, all passing):
```cpp
// tests/test_bus_port_swap.cpp
TEST(BusPortSwapTest, SwapTwoAliasPorts_ChangesOrder) { /* PASSES */ }
TEST(BusPortSwapTest, SwapAdjacentPorts) { /* PASSES */ }
// ... 5 more tests
```

## The Problem (What's Broken)

### User Action
1. User clicks and drags a wire end from bus port "wire_1"
2. Drags it to bus port "wire_2" (same bus)
3. Releases mouse

### Expected Behavior
- Ports swap positions (port "wire_1" moves to "wire_2"'s slot and vice versa)
- Wires remain connected and follow the ports to new positions
- Routing points are preserved or recalculated correctly

### Actual Behavior
- ❌ Ports swap (this works!)
- ❌ **Wires remain disconnected** - they don't follow the ports
- ❌ **Routing points are lost** - wires snap to straight lines or wrong positions

### Root Cause Analysis

**Issue 1: VisualPort worldPosition()**
When `BusVisualNode::distributePortsInRow()` is called after swap, it recalculates port positions:
```cpp
void BusVisualNode::distributePortsInRow(const std::vector<Wire>& wires) {
    ports_.clear();
    // Add alias ports (named by wire_id)
    for (const auto& w : wires) {
        VisualPort vp(w.id, PortSide::InOut, an24::PortType::V, "v");
        vp.setWorldPosition(calculatePortPosition(ports_.size()));  // Position by index
        ports_.push_back(std::move(vp));
    }
    // Add logical "v" port at the end
    // ...
}
```

**Problem:** After swap, `calculatePortPosition(i)` returns different positions for the same wire_id because the index changed!

**Issue 2: Wire resolution**
When rendering wires, the system uses `resolveWirePort()`:
```cpp
const VisualPort* BusVisualNode::resolveWirePort(const std::string& port_name,
                                                  const char* wire_id) const {
    if (port_name == "v" && wire_id != nullptr) {
        for (const auto& p : ports_) {
            if (p.name() == wire_id) return &p;  // Find alias port by wire_id
        }
    }
    return getPort(port_name);
}
```

**Problem:** This finds the correct VisualPort, but that VisualPort's `worldPosition()` is wrong after swap because ports were reordered.

**Issue 3: Wire routing**
Current implementation clears routing points and auto-routes:
```cpp
// In swapWirePortsOnBus():
wire_a.routing_points.clear();
wire_b.routing_points.clear();
WireManager wm(*this);
wm.routeWire(idx_a);
wm.routeWire(idx_b);
```

**Problem:** Auto-routing calculates paths based on current port positions, but those positions are stale (calculated before swap).

## Required Solution

### Goal
Make wires **visually follow** their ports when bus ports are swapped. Wire endpoints should stay attached to their ports after swap.

### Acceptance Criteria
1. ✅ User drags wire from port A to port B (same bus)
2. ✅ Ports swap positions (already works)
3. ✅ **Wires remain attached** to their ports (BROKEN)
4. ✅ **Wire endpoints update** to new port positions (BROKEN)
5. ✅ **Routing is preserved** or correctly recalculated (BROKEN)

### Technical Requirements

#### Option A: Update Wire Endpoints After Swap
When ports are swapped, wire endpoints need to know the new port positions:

```cpp
// Pseudo-code
bool VisualScene::swapWirePortsOnBus(...) {
    // 1. Get current port positions BEFORE swap
    Pt port_a_pos_before = bus_vis->resolveWirePort("v", wire_id_a)->worldPosition();
    Pt port_b_pos_before = bus_vis->resolveWirePort("v", wire_id_b)->worldPosition();

    // 2. Swap visual ports
    bus_vis->handlePortSwap(wire_id_a, wire_id_b);

    // 3. Get NEW port positions AFTER swap
    Pt port_a_pos_after = bus_vis->resolveWirePort("v", wire_id_a)->worldPosition();
    Pt port_b_pos_after = bus_vis->resolveWirePort("v", wire_id_b)->worldPosition();

    // 4. Update wire endpoints to use new positions
    // QUESTION: How? Wire objects don't store port positions directly!

    // 5. Re-route wires with new positions
    routeWire(idx_a);
    routeWire(idx_b);
}
```

**Key Question:** How do wire endpoints know to use the new port positions?

#### Option B: Recalculate All Port World Positions
After swap, force recalculation of all `VisualPort::worldPosition()` values:

```cpp
// Pseudo-code
void BusVisualNode::swapAliasPorts(...) {
    std::swap(wires_[idx_a], wires_[idx_b]);
    distributePortsInRow(wires_);  // This calls calculatePortPosition()

    // NEW: Force recalculation of world positions for all ports
    for (size_t i = 0; i < ports_.size(); i++) {
        ports_[i].setWorldPosition(calculatePortPosition(i));
    }

    // NEW: Notify connected wires that port positions changed
    // QUESTION: How to notify wires?
}
```

**Key Question:** How to notify wires that their connected ports moved?

#### Option C: Store Port Positions in Wire Objects
Modify `Wire` struct to store port world positions:

```cpp
// data/wire.h
struct WireEnd {
    char node_id[32];
    char port_name[32];
    PortSide side;
    Pt cached_port_position;  // NEW: Cache port world position
};
```

Update cached positions when:
- Node moves
- Ports are swapped
- Wire is reconnected

**Key Question:** Is this the right approach? Adds complexity.

## Files to Modify

### Core Files
- `/Users/vladimir/an24_cpp/src/editor/visual/node/node.h` (200 lines) - BusVisualNode
- `/Users/vladimir/an24_cpp/src/editor/visual/node/node.cpp` (600 lines) - Implementation
- `/Users/vladimir/an24_cpp/src/editor/visual/scene/scene.h` (270 lines) - VisualScene::swapWirePortsOnBus()
- `/Users/vladimir/an24_cpp/src/editor/input/canvas_input.cpp` (450 lines) - Input handling

### Related Files
- `/Users/vladimir/an24_cpp/src/editor/visual/port/port.h` - VisualPort class
- `/Users/vladimir/an24_cpp/src/editor/data/wire.h` - Wire, WireEnd structs
- `/Users/vladimir/an24_cpp/src/editor/visual/scene/wire_manager.h` - Wire routing

### Test Files
- `/Users/vladimir/an24_cpp/tests/test_bus_port_swap.cpp` - Existing tests (all passing)

## Investigation Steps

1. **Understand wire rendering pipeline**
   - How does `WireRenderer` get port positions?
   - Does it cache positions or call `resolveWirePort()` every frame?
   - Check: `/Users/vladimir/an24_cpp/src/editor/visual/renderer/wire_renderer.cpp`

2. **Understand port position lifecycle**
   - When are `VisualPort::worldPosition()` values calculated?
   - Are they cached or recalculated every frame?
   - What triggers recalculation?

3. **Add debug logging**
   ```cpp
   // In swapWirePortsOnBus():
   spdlog::info("Before swap: port_a={}, pos=({},{})", port_a, pos_a.x, pos_a.y);
   bus_vis->handlePortSwap(...);
   spdlog::info("After swap: port_a={}, pos=({},{})", port_a, new_pos_a.x, new_pos_a.y);
   ```

4. **Check if cache invalidation is needed**
   - Does `VisualNodeCache` need to be cleared after swap?
   - Are visual nodes being recreated instead of updated?

## Expected Output

A working solution where:
1. Bus port swap works as expected
2. Wires remain attached to their ports after swap
3. Wire endpoints follow ports to new positions
4. All 7 existing tests continue to pass
5. Optional: New integration test verifying wire attachment after swap

## Code Context

### Bus Port System
```cpp
// Each wire connected to bus gets an alias port:
// Wire "w1" → alias port with name="w1", target_port="v"
// Wire "w2" → alias port with name="w2", target_port="v"
// Wire "w3" → alias port with name="w3", target_port="v"
// Logical:   port with name="v", target_port="" (for new connections)

// Before swap: [w1_port][w2_port][w3_port][v_port]
// After swap w1↔w3: [w3_port][w2_port][w1_port][v_port]

// Wire resolution:
resolveWirePort("v", "w1") → finds port named "w1" (alias for wire w1)
```

### Wire Rendering
```cpp
// WireRenderer draws line from:
// - Wire.start.node_id + Wire.start.port_name → resolve to VisualPort
// - Wire.end.node_id + Wire.end.port_name → resolve to VisualPort
// - Use VisualPort::worldPosition() for endpoints
// - Use Wire.routing_points for intermediate points
```

## Success Metrics

- [ ] Swap operation completes without errors
- [ ] Wires visually follow ports to new positions (verified in running editor)
- [ ] Wire endpoints are correctly positioned at port locations
- [ ] Routing points are preserved or correctly recalculated
- [ ] All unit tests pass (7 bus port swap tests + existing tests)
- [ ] No memory leaks or crashes
- [ ] Solution is minimal and doesn't break existing functionality

## Notes

- This is a **visual-only issue** - Wire data structures don't need modification
- BusVisualNode's `wires_` vector is a **visual copy** - not the authoritative wire storage
- The authoritative wires are in `Blueprint::wires` (accessed via `scene_.wires()`)
- Focus on **port position synchronization** between visual and data layers

---
**Please analyze the codebase, identify the root cause, and implement a fix that makes wires follow their ports during bus port swap.**
