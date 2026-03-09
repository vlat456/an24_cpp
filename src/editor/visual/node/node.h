#pragma once

#include "visual/interfaces.h"
#include "data/pt.h"
#include "data/node.h"
#include "data/wire.h"
#include "visual/port/port.h"
#include "visual/node/widget.h"
#include "viewport/viewport.h"
#include "json_parser/json_parser.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

// Forward declaration
struct EditorApp;

// Forward declaration (IDrawList is defined in visual/renderer/draw_list.h)
struct IDrawList;

// ============================================================================
// VisualNode — visual representation of a circuit node
// ============================================================================
// Concrete class with ColumnLayout-based rendering.
// Subclasses (BusVisualNode, RefVisualNode) override render() and port management.
//
// Port positions are stored in VisualPort::worldPosition().
// Consumers access ports directly — no getPortPosition() convenience wrapper.

class VisualNode : public IDrawable, public ISelectable,
                   public IDraggable, public IPersistable {
public:
    VisualNode(const Node& node);
    virtual ~VisualNode() = default;

    // --- IDraggable ---
    Pt getPosition() const override { return position_; }
    void setPosition(Pt pos) override;
    Pt getSize() const override { return size_; }
    // [t4u5v6w7] Snap size to grid so bottom-right corner stays grid-aligned
    void setSize(Pt size) override;

    // --- IPersistable ---
    const std::string& getId() const override { return node_id_; }

    // --- ISelectable ---
    bool containsPoint(Pt world_pos) const override {
        return world_pos.x >= position_.x && world_pos.x <= position_.x + size_.x &&
               world_pos.y >= position_.y && world_pos.y <= position_.y + size_.y;
    }

    // --- Port access ---
    const VisualPort* getPort(const std::string& name) const;
    const VisualPort* getPort(size_t index) const;
    size_t getPortCount() const { return ports_.size(); }
    std::vector<std::string> getPortNames() const;

    /// Resolve which port a wire endpoint should connect to.
    /// For normal nodes, returns getPort(port_name).
    /// Bus overrides to resolve alias ports by wire_id.
    virtual const VisualPort* resolveWirePort(const std::string& port_name,
                                               const char* wire_id = nullptr) const;

    // --- Wire connection management (Bus overrides for dynamic ports) ---
    virtual void connectWire(const Wire& wire);
    virtual void disconnectWire(const Wire& wire);
    virtual void recalculatePorts();

    // --- Content access ---
    NodeContentType getContentType() const { return node_content_.type; }
    const NodeContent& getNodeContent() const { return node_content_; }
    Bounds getContentBounds() const;
    virtual void updateNodeContent(const NodeContent& content) { node_content_ = content; }

    // --- Layout access for testing ---
    const ColumnLayout& getLayout() const { return layout_; }

    // --- IDrawable ---
    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

protected:
    Pt position_;
    Pt size_;
    std::string node_id_;
    std::vector<VisualPort> ports_;
    std::string name_;
    std::string type_name_;
    NodeContent node_content_;
    ColumnLayout layout_;
    std::vector<PortRowWidget*> port_rows_;

    void buildLayout(const Node& node);
    void buildPorts(const Node& node);
};

// ============================================================================
// BusVisualNode — Bus with dynamic ports
// ============================================================================

enum class BusOrientation {
    Horizontal,
    Vertical
};

class BusVisualNode : public VisualNode {
public:
    BusVisualNode(const Node& node, BusOrientation orientation = BusOrientation::Horizontal,
                  const std::vector<Wire>& wires = {});

    const VisualPort* resolveWirePort(const std::string& port_name,
                                       const char* wire_id = nullptr) const override;

    void connectWire(const Wire& wire) override;
    void disconnectWire(const Wire& wire) override;
    void recalculatePorts() override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

private:
    BusOrientation orientation_;
    std::vector<Wire> wires_;

    void distributePortsInRow(const std::vector<Wire>& wires = {});
    Pt calculatePortPosition(size_t index) const;
    Pt calculateBusSize(size_t port_count) const;
};

// ============================================================================
// RefVisualNode — Reference node with single port on top
// ============================================================================

class RefVisualNode : public VisualNode {
public:
    RefVisualNode(const Node& node);

    void recalculatePorts() override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;
};

// ============================================================================
// Factory
// ============================================================================

class VisualNodeFactory {
public:
    static std::unique_ptr<VisualNode> create(const Node& node,
                                               const std::vector<Wire>& wires = {}) {
        switch (node.kind) {
            case NodeKind::Bus:
                return std::make_unique<BusVisualNode>(node, BusOrientation::Horizontal, wires);
            case NodeKind::Ref:
                return std::make_unique<RefVisualNode>(node);
            case NodeKind::Blueprint:
                {
                    Node bp_node = node;
                    bp_node.node_content = NodeContent{};
                    return std::make_unique<VisualNode>(bp_node);
                }
            case NodeKind::InternalCPP:
                {
                    // Same visual as Blueprint but not expandable
                    Node cpp_node = node;
                    cpp_node.node_content = NodeContent{};
                    return std::make_unique<VisualNode>(cpp_node);
                }
            case NodeKind::Node:
            default:
                return std::make_unique<VisualNode>(node);
        }
    }
};

// ============================================================================
// VisualNodeCache
// ============================================================================

class VisualNodeCache {
public:
    VisualNodeCache() = default;

    VisualNode* getOrCreate(const Node& node, const std::vector<Wire>& wires = {});
    VisualNode* get(const std::string& node_id);
    void clear() { cache_.clear(); }

    void onWireAdded(const Wire& wire, const std::vector<Node>& all_nodes);
    void onWireDeleted(const Wire& wire, const std::vector<Node>& all_nodes);

private:
    std::unordered_map<std::string, std::unique_ptr<VisualNode>> cache_;
};
