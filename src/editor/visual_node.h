#pragma once

#include "interfaces.h"
#include "data/pt.h"
#include "data/node.h"
#include "data/wire.h"
#include "widget.h"
#include "viewport/viewport.h"
#include "../json_parser/json_parser.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

// Forward declaration
struct EditorApp;

// Forward declaration (IDrawList is defined in render.h)
struct IDrawList;

// ============================================================================
// BaseVisualNode - Abstract base class for all visual node types
// Implements: IDrawable, ISelectable, IDraggable, IPersistable
// ============================================================================

class BaseVisualNode : public IDrawable, public ISelectable,
                       public IDraggable, public IPersistable {
public:
    virtual ~BaseVisualNode() = default;

    // --- IDraggable ---
    Pt getPosition() const override { return position_; }
    void setPosition(Pt pos) override { position_ = pos; }
    Pt getSize() const override { return size_; }
    // [t4u5v6w7] Snap size to grid so bottom-right corner stays grid-aligned
    void setSize(Pt size) override;

    // --- IPersistable ---
    const std::string& getId() const override { return node_id_; }

    // --- Visibility control ---
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }

    // --- ISelectable ---
    bool containsPoint(Pt world_pos) const override {
        return world_pos.x >= position_.x && world_pos.x <= position_.x + size_.x &&
               world_pos.y >= position_.y && world_pos.y <= position_.y + size_.y;
    }

    struct Port {
        std::string name;           ///< Visual port name (e.g., wire ID for dynamic aliases)
        std::string target_port;    ///< Target logical port (empty if same as name)
        Pt world_position;
        an24::PortType type = an24::PortType::Any;  ///< Port type for visualization
    };

    // Port access
    virtual const Port* getPort(const std::string& name) const = 0;
    virtual const Port* getPort(size_t index) const = 0;
    virtual size_t getPortCount() const = 0;
    virtual std::vector<std::string> getPortNames() const = 0;

    // Port position in world coordinates
    virtual Pt getPortPosition(const std::string& port_name,
                               const char* wire_id = nullptr) const = 0;

    // Wire connection management (for dynamic bus ports)
    virtual void connectWire(const Wire& wire) = 0;
    virtual void disconnectWire(const Wire& wire) = 0;
    virtual void recalculatePorts() = 0;

    // Content access (overrides in StandardVisualNode)
    virtual NodeContentType getContentType() const { return NodeContentType::None; }
    virtual const NodeContent& getNodeContent() const {
        static NodeContent empty;
        return empty;
    }
    virtual Bounds getContentBounds() const { return {}; }

    // Update node content from blueprint (called each frame for cached nodes)
    virtual void updateNodeContent(const NodeContent&) {}

    // --- IDrawable --- render() is pure virtual from IDrawable

protected:
    BaseVisualNode(const Node& node);

    Pt position_;
    Pt size_;
    std::string node_id_;
    std::vector<Port> ports_;
    bool visible_ = true;  ///< Visibility flag for view filtering (true = render this node)
};

// ============================================================================
// StandardVisualNode - Regular nodes with ColumnLayout-based rendering
// ============================================================================

class StandardVisualNode : public BaseVisualNode {
public:
    StandardVisualNode(const Node& node);

    const Port* getPort(const std::string& name) const override;
    const Port* getPort(size_t index) const override;
    size_t getPortCount() const override;
    std::vector<std::string> getPortNames() const override;

    Pt getPortPosition(const std::string& port_name,
                       const char* wire_id = nullptr) const override;

    void connectWire(const Wire& wire) override;
    void disconnectWire(const Wire& wire) override;
    void recalculatePorts() override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

    // Content access
    NodeContentType getContentType() const override;
    const NodeContent& getNodeContent() const override;
    Bounds getContentBounds() const override;
    void updateNodeContent(const NodeContent& content) override { node_content_ = content; }

    // Access to layout for testing
    const ColumnLayout& getLayout() const { return layout_; }

private:
    std::vector<Port> inputs_;
    std::vector<Port> outputs_;
    std::string name_;
    std::string type_name_;
    NodeContent node_content_;

    ColumnLayout layout_;
    std::vector<PortRowWidget*> port_rows_;

    void buildLayout();
};

// ============================================================================
// BusVisualNode - Bus with dynamic ports
// ============================================================================

enum class BusOrientation {
    Horizontal,
    Vertical
};

class BusVisualNode : public BaseVisualNode {
public:
    BusVisualNode(const Node& node, BusOrientation orientation = BusOrientation::Horizontal,
                  const std::vector<Wire>& wires = {});

    // Use base class setSize() - size comes from component definition default_size

    const Port* getPort(const std::string& name) const override;
    const Port* getPort(size_t index) const override;
    size_t getPortCount() const override { return ports_.size(); }
    std::vector<std::string> getPortNames() const override;

    Pt getPortPosition(const std::string& port_name,
                       const char* wire_id = nullptr) const override;

    void connectWire(const Wire& wire) override;
    void disconnectWire(const Wire& wire) override;
    void recalculatePorts() override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

private:
    BusOrientation orientation_;
    std::string name_;
    std::vector<Wire> wires_;

    void distributePortsInRow(const std::vector<Wire>& wires = {});
    Pt calculatePortPosition(size_t index) const;
    Pt calculateBusSize(size_t port_count) const;
};

// ============================================================================
// RefVisualNode - Reference node with single port on top
// ============================================================================

class RefVisualNode : public BaseVisualNode {
public:
    RefVisualNode(const Node& node);

    // Use base class setSize() - size comes from component definition default_size

    const Port* getPort(const std::string& name) const override;
    const Port* getPort(size_t index) const override;
    size_t getPortCount() const override { return ports_.size(); }
    std::vector<std::string> getPortNames() const override;

    Pt getPortPosition(const std::string& port_name,
                       const char* wire_id = nullptr) const override;

    void connectWire(const Wire& wire) override;
    void disconnectWire(const Wire& wire) override;
    void recalculatePorts() override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

private:
    std::string name_;
};

// ============================================================================
// Factory
// ============================================================================

class VisualNodeFactory {
public:
    static std::unique_ptr<BaseVisualNode> create(const Node& node,
                                                   const std::vector<Wire>& wires = {}) {
        switch (node.kind) {
            case NodeKind::Bus:
                return std::make_unique<BusVisualNode>(node, BusOrientation::Horizontal, wires);
            case NodeKind::Ref:
                return std::make_unique<RefVisualNode>(node);
            case NodeKind::Blueprint:
                // Collapsed blueprint nodes render like standard nodes with exposed ports
                // but never have content (no gauges, switches, text labels)
                {
                    Node bp_node = node;
                    bp_node.node_content = NodeContent{};
                    return std::make_unique<StandardVisualNode>(bp_node);
                }
            case NodeKind::Node:
            default:
                return std::make_unique<StandardVisualNode>(node);
        }
    }
};

// ============================================================================
// VisualNodeCache
// ============================================================================

class VisualNodeCache {
public:
    VisualNodeCache() = default;

    BaseVisualNode* getOrCreate(const Node& node, const std::vector<Wire>& wires = {});
    BaseVisualNode* get(const std::string& node_id);
    void clear() { cache_.clear(); }

    void onWireAdded(const Wire& wire, const std::vector<Node>& all_nodes);
    void onWireDeleted(const Wire& wire, const std::vector<Node>& all_nodes);

private:
    std::unordered_map<std::string, std::unique_ptr<BaseVisualNode>> cache_;
};
