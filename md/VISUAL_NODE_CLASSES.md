# Visual Node Classes Design

## Goal

Refactor node rendering from enum-based to class-based with inheritance:
- `BaseVisualNode` - abstract base with common functionality
- `StandardVisualNode` - regular nodes with ports on sides
- `BusVisualNode` - bus/multiplexer with dynamic ports on perimeter
- `RefVisualNode` - reference node with single port on top

## Architecture

```cpp
// Forward declarations
struct Node;
struct Wire;
struct Blueprint;

// ============================================================================
// BaseVisualNode - Abstract base class
// ============================================================================

class BaseVisualNode {
public:
    virtual ~BaseVisualNode() = default;

    // Properties
    Pt getPosition() const { return position_; }
    void setPosition(Pt pos) { position_ = pos; }

    Pt getSize() const { return size_; }
    void setSize(Pt size) { size_ = size; }

    // Port management - array access
    // Note: Bus ports have NO input/output distinction - just memory addresses
    struct Port {
        std::string name;       // port identifier (wire_id for Bus, "in1"/"out2" for Standard)
        Pt world_position;      // cached world position
        // side is optional - only needed for StandardVisualNode
        std::optional<PortSide> side;
    };

    virtual const Port* getPort(const std::string& name) const = 0;
    virtual const Port* getPort(size_t index) const = 0;
    virtual size_t getPortCount() const = 0;
    virtual std::vector<std::string> getPortNames() const = 0;

    // Port position lookup
    virtual Pt getPortPosition(const std::string& port_name) const = 0;

    // Wire connection management (for dynamic systems)
    virtual void connectWire(const Wire& wire) = 0;
    virtual void disconnectWire(const Wire& wire) = 0;
    virtual void recalculatePorts() = 0;

    // Rendering (virtual - override in derived classes)
    virtual void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                       bool is_selected) const = 0;

    // Factory method
    static std::unique_ptr<BaseVisualNode> create(const Node& node);

protected:
    Pt position_;
    Pt size_;
    std::string node_id_;
    std::vector<Port> ports_;  // dynamic port array
};

// ============================================================================
// StandardVisualNode - Ports on left/right sides
// ============================================================================

class StandardVisualNode : public BaseVisualNode {
public:
    StandardVisualNode(const Node& node);

    Pt getPortPosition(const std::string& port_name) const override;
    std::vector<std::string> getPortNames() const override;
    size_t getPortCount() const override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

private:
    static constexpr float HEADER_HEIGHT = 20.0f;

    std::vector<Port> inputs_;
    std::vector<Port> outputs_;
    std::string name_;
    std::string type_name_;
};

// ============================================================================
// BusVisualNode - Single row of ports (input OR output - any can be either)
// ============================================================================

// Bus visual orientation
enum class BusOrientation {
    Horizontal,  // ports on right side
    Vertical     // ports on bottom side
};

class BusVisualNode : public BaseVisualNode {
public:
    BusVisualNode(const Node& node, BusOrientation orientation = BusOrientation::Horizontal);

    // BaseVisualNode interface
    const Port* getPort(const std::string& name) const override;
    const Port* getPort(size_t index) const override;
    size_t getPortCount() const override { return ports_.size(); }
    std::vector<std::string> getPortNames() const override;

    Pt getPortPosition(const std::string& port_name) const override;

    // Wire connection management
    void connectWire(const Wire& wire) override;
    void disconnectWire(const Wire& wire) override;
    void recalculatePorts() override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

private:
    BusOrientation orientation_;

    // Distribute ports in single row
    void distributePortsInRow();

    // Calculate position for port at index
    Pt calculatePortPosition(size_t index) const;

    // Get size based on port count
    Pt calculateBusSize(size_t port_count) const;

    std::vector<Port> ports_;  // no input/output distinction - just addresses
};

// ============================================================================
// RefVisualNode - Single port on top
// ============================================================================

class RefVisualNode : public BaseVisualNode {
public:
    RefVisualNode(const Node& node);

    Pt getPortPosition(const std::string& port_name) const override;
    std::vector<std::string> getPortNames() const override;
    size_t getPortCount() const override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;
};

// ============================================================================
// Factory
// ============================================================================

class VisualNodeFactory {
public:
    static std::unique_ptr<BaseVisualNode> create(
        const Node& node,
        const std::vector<Wire>& all_wires = {}) {

        switch (node.kind) {
            case NodeKind::Bus:
                return std::make_unique<BusVisualNode>(node, all_wires);
            case NodeKind::Ref:
                return std::make_unique<RefVisualNode>(node);
            case NodeKind::Node:
            default:
                return std::make_unique<StandardVisualNode>(node);
        }
    }
};
```

## Wire Connection API

Each visual node supports dynamic wire connections:

```cpp
// Connect a wire to this node
void connectWire(const Wire& wire) {
    // Determine which side (start/end) connects to this node
    if (wire.start.node_id == node_id_) {
        ports_.push_back({
            .name = wire.id,              // use wire ID as port name for Bus
            .world_position = calculatePortPosition(ports_.size())
        });
    }
    if (wire.end.node_id == node_id_) {
        ports_.push_back({
            .name = wire.id,
            .world_position = calculatePortPosition(ports_.size())
        });
    }
    recalculatePorts();
}

// Disconnect a wire from this node
void disconnectWire(const Wire& wire) {
    ports_.erase(
        std::remove_if(ports_.begin(), ports_.end(),
            [&](const Port& p) { return p.name == wire.id; }),
        ports_.end());
    recalculatePortPositions();
}
```

### 1. Factory Pattern
Use factory to create correct node type based on `NodeKind`:
```cpp
auto visual = VisualNodeFactory::create(node, wires);
```

### 2. Bus-Specific Constructor
BusVisualNode needs all wires to count connections:
```cpp
BusVisualNode(const Node& node, const std::vector<Wire>& all_wires) {
    // Count wires connected to this bus
    // Calculate port positions
}
```

### 3. Port Position Caching
Bus ports calculated once and cached:
```cpp
// In BusVisualNode
void recalculatePorts(const std::vector<Wire>& all_wires) {
    auto connections = countConnections(node_id_, all_wires);
    auto positions = calculatePortPositions(connections.size());
    // Cache: port_name -> position
}
```

### 4. Update Triggers
When wires change, Bus ports need recalculation:
```cpp
// In EditorApp
void onWireAdded(const Wire& w) {
    // Find Bus nodes connected to w
    // Call recalculatePorts()
}
```

## Port Distribution Algorithm (Bus)

**Bus = single row of ports** - no input/output distinction. Each port is just a memory address.

- **Horizontal**: one row of ports on RIGHT side
- **Vertical**: one row of ports on BOTTOM side

```cpp
// Horizontal Bus: ═══ one row on right side
// ┌─────────────┐
// │             │
// │             │  ← bus body
// │           ◉│  ← single row of ports
// │           ◉│
// └─────────────┘

// Vertical Bus: ║ one row on bottom
// ┌─────────────┐
// │             │
// │             │  ← bus body
// │             │
// └─────────────┘
//   ◉ ◉ ◉ ◉ ◉   ← single row of ports

Pt BusVisualNode::calculatePortPosition(size_t index) const {
    float margin = 10.0f;   // padding from edges
    float spacing = 20.0f;  // space between ports

    float x, y;

    if (orientation_ == BusOrientation::Horizontal) {
        // Single row on RIGHT side
        x = position_.x + size_.x;  // right edge

        float min_y = position_.y + margin;
        float max_y = position_.y + size_.y - margin;
        float step = (max_y - min_y) / (port_count_ + 1);
        y = min_y + step * (index + 1);
    } else {
        // Single row on BOTTOM side
        y = position_.y + size_.y;  // bottom edge

        float min_x = position_.x + margin;
        float max_x = position_.x + size_.x - margin;
        float step = (max_x - min_x) / (port_count_ + 1);
        x = min_x + step * (index + 1);
    }

    return Pt(x, y);
}
```

**No input/output** - Bus ports are just addresses in memory. Any wire can connect to any port.

// Dynamic size based on port count
Pt BusVisualNode::calculateBusSize(size_t port_count) const {
    float width, height;
    float min_size = 40.0f;
    float port_spacing = 20.0f;

    if (bus_type_ == BusType::Horizontal) {
        width = std::max(min_size, port_count * port_spacing);
        height = 40.0f;  // fixed height
    } else {
        width = 40.0f;  // fixed width
        height = std::max(min_size, port_count * port_spacing);
    }

    return Pt(width, height);
}
```

## Integration with Existing Code

### EditorApp Integration

```cpp
// In EditorApp - when wires change, update visual nodes
class VisualNodeCache {
public:
    std::unique_ptr<BaseVisualNode>& getOrCreate(const Node& node) {
        auto it = cache_.find(node.id);
        if (it == cache_.end()) {
            cache_[node.id] = VisualNodeFactory::create(node, blueprint_.wires);
            return cache_[node.id];
        }
        return it->second;
    }

    void onWireAdded(const Wire& w) {
        // Find nodes connected to this wire
        auto& start_node = getOrCreate(findNode(w.start.node_id));
        auto& end_node = getOrCreate(findNode(w.end.node_id));
        start_node->connectWire(w);
        end_node->connectWire(w);
    }

    void onWireDeleted(const Wire& w) {
        auto& start_node = getOrCreate(findNode(w.start.node_id));
        auto& end_node = getOrCreate(findNode(w.end.node_id));
        start_node->disconnectWire(w);
        end_node->disconnectWire(w);
    }

private:
    std::unordered_map<std::string, std::unique_ptr<BaseVisualNode>> cache_;
    Blueprint& blueprint_;
};
```

### render.cpp - Replace switch with polymorphism
```cpp
// OLD
if (n.kind == NodeKind::Bus) { ... }
else if (n.kind == NodeKind::Ref) { ... }
else { ... }

// NEW
auto visual = VisualNodeFactory::create(n, bp.wires);
visual->render(dl, vp, canvas_min, is_selected);
```

### trigonometry.h - Replace with virtual method
```cpp
// OLD
Pt get_port_position(const Node& node, const char* port_name) {
    if (node.kind == NodeKind::Bus) { ... }
}

// NEW
auto visual = VisualNodeFactory::create(node, wires);
return visual->getPortPosition(port_name);
```

## Benefits

1. **Open/Closed Principle** - add new node types without modifying existing code
2. **Single Responsibility** - each class handles its own rendering
3. **Testability** - mock visual nodes for testing
4. **Bus Dynamic Ports** - built into BusVisualNode naturally
5. **Future Extensibility** - easy to add new behaviors

## Files to Create/Modify

### New Files
- `src/editor/visual_node.h` - base class + factory + VisualNodeCache
- `src/editor/visual_node.cpp` - implementations

### Modified Files
- `src/editor/render.cpp` - use VisualNodeFactory
- `src/editor/trigonometry.h` - use VisualNodeFactory
- `src/editor/app.h` - add VisualNodeCache member
- `src/editor/app.cpp` - wire add/remove triggers connectWire/disconnectWire

## Testing Strategy

1. **Unit tests** for each VisualNode class
2. **Port position tests** - verify correct distribution
3. **Bus wire count tests** - verify dynamic port count
4. **Integration tests** - verify render output
