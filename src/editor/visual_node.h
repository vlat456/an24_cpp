#pragma once

#include "data/pt.h"
#include "data/node.h"
#include "data/wire.h"
#include "widget.h"
#include "viewport/viewport.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

// Forward declaration (IDrawList is defined in render.h)
struct IDrawList;

// ============================================================================
// BaseVisualNode - Abstract base class for all visual node types
// ============================================================================

class BaseVisualNode {
public:
    virtual ~BaseVisualNode() = default;

    Pt getPosition() const { return position_; }
    void setPosition(Pt pos) { position_ = pos; }

    Pt getSize() const { return size_; }
    void setSize(Pt size) { size_ = size; }

    const std::string& getId() const { return node_id_; }

    struct Port {
        std::string name;
        Pt world_position;
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

    // Rendering
    virtual void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                       bool is_selected) const = 0;

protected:
    BaseVisualNode(const Node& node);

    Pt position_;
    Pt size_;
    std::string node_id_;
    std::vector<Port> ports_;
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

    // Access to layout for testing
    const ColumnLayout& getLayout() const { return layout_; }

private:
    std::vector<Port> inputs_;
    std::vector<Port> outputs_;
    std::string name_;
    std::string type_name_;
    NodeContent node_content_;

    ColumnLayout layout_;
    std::vector<PortRowWidget*> port_rows_;  // non-owning pointers into layout

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

    BaseVisualNode* getOrCreate(const Node& node);
    BaseVisualNode* get(const std::string& node_id);
    void clear() { cache_.clear(); }

    void onWireAdded(const Wire& wire, const std::vector<Node>& all_nodes);
    void onWireDeleted(const Wire& wire, const std::vector<Node>& all_nodes);

private:
    std::unordered_map<std::string, std::unique_ptr<BaseVisualNode>> cache_;
};
