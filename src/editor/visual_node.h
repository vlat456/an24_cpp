#pragma once

#include "data/pt.h"
#include "data/node.h"
#include "data/wire.h"
#include "viewport/viewport.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <optional>

// Forward declaration (IDrawList is defined in render.h)
struct IDrawList;

// Forward declaration for ImGui (will be implemented in example)
// Using void* to avoid coupling with imgui.h in header
using ImGuiWindow = void;

// ============================================================================
// BaseVisualNode - Abstract base class for all visual node types
// ============================================================================

class BaseVisualNode {
public:
    virtual ~BaseVisualNode() = default;

    // Properties
    Pt getPosition() const { return position_; }
    void setPosition(Pt pos) { position_ = pos; }

    Pt getSize() const { return size_; }
    void setSize(Pt size) { size_ = size; }  // Size should already be snapped

    const std::string& getId() const { return node_id_; }

    // Port struct - no input/output distinction for Bus
    struct Port {
        std::string name;              // port identifier (wire_id for Bus)
        Pt world_position;             // cached world position
    };

    // Port access
    virtual const Port* getPort(const std::string& name) const = 0;
    virtual const Port* getPort(size_t index) const = 0;
    virtual size_t getPortCount() const = 0;
    virtual std::vector<std::string> getPortNames() const = 0;

    // Port position lookup
    virtual Pt getPortPosition(const std::string& port_name, const char* wire_id = nullptr) const = 0;

    // Wire connection management
    virtual void connectWire(const Wire& wire) = 0;
    virtual void disconnectWire(const Wire& wire) = 0;
    virtual void recalculatePorts() = 0;

    // Rendering - DrawList (background, borders, ports, labels)
    virtual void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                       bool is_selected) const = 0;

    // Rendering - ImGui widgets for content (interactive elements)
    // Returns the world-space area available for content
    virtual Pt renderContent(ImGuiWindow* imgui, const Viewport& vp, Pt canvas_min,
                            float screen_content_min_x, float screen_content_max_x) const = 0;

    // Get content type for this node
    virtual NodeContentType getContentType() const { return NodeContentType::None; }

protected:
    BaseVisualNode(const Node& node);

    Pt position_;
    Pt size_;
    std::string node_id_;
    std::vector<Port> ports_;
};

// ============================================================================
// StandardVisualNode - Regular nodes with ports on left/right sides
// ============================================================================

class StandardVisualNode : public BaseVisualNode {
public:
    StandardVisualNode(const Node& node);

    const Port* getPort(const std::string& name) const override;
    const Port* getPort(size_t index) const override;
    size_t getPortCount() const override { return ports_.size(); }
    std::vector<std::string> getPortNames() const override;

    Pt getPortPosition(const std::string& port_name, const char* wire_id = nullptr) const override;

    void connectWire(const Wire& wire) override;
    void disconnectWire(const Wire& wire) override;
    void recalculatePorts() override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

    Pt renderContent(ImGuiWindow* imgui, const Viewport& vp, Pt canvas_min,
                  float screen_content_min_x, float screen_content_max_x) const override;

    NodeContentType getContentType() const override { return node_content_.type; }

private:
    static constexpr float HEADER_HEIGHT = 20.0f;
    static constexpr float CONTENT_MARGIN = 8.0f;  // Margin around content

    std::vector<Port> inputs_;
    std::vector<Port> outputs_;
    std::string name_;
    std::string type_name_;
    NodeContent node_content_;

    Pt calculatePortPosition(size_t index, PortSide side) const;

    // Calculate safe content area that doesn't overlap with port labels
    Pt calculateContentArea(const Viewport& vp, Pt canvas_min,
                           float screen_content_min_x, float screen_content_max_x) const;
};

// ============================================================================
// BusVisualNode - Bus with single row of ports (visual addresses)
// ============================================================================

// Bus visual orientation
enum class BusOrientation {
    Horizontal,  // ports on right side
    Vertical    // ports on bottom side
};

class BusVisualNode : public BaseVisualNode {
public:
    BusVisualNode(const Node& node, BusOrientation orientation = BusOrientation::Horizontal,
                  const std::vector<Wire>& wires = {});

    const Port* getPort(const std::string& name) const override;
    const Port* getPort(size_t index) const override;
    size_t getPortCount() const override { return ports_.size(); }
    std::vector<std::string> getPortNames() const override;

    Pt getPortPosition(const std::string& port_name, const char* wire_id = nullptr) const override;

    void connectWire(const Wire& wire) override;
    void disconnectWire(const Wire& wire) override;
    void recalculatePorts() override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

    Pt renderContent(ImGuiWindow* imgui, const Viewport& vp, Pt canvas_min,
                  float screen_content_min_x, float screen_content_max_x) const override;

private:
    BusOrientation orientation_;
    std::string name_;
    std::vector<Wire> wires_;  // Store wires for port lookup

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

    Pt getPortPosition(const std::string& port_name, const char* wire_id = nullptr) const override;

    void connectWire(const Wire& wire) override;
    void disconnectWire(const Wire& wire) override;
    void recalculatePorts() override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

    Pt renderContent(ImGuiWindow* imgui, const Viewport& vp, Pt canvas_min,
                  float screen_content_min_x, float screen_content_max_x) const override;

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
// VisualNodeCache - Cache for visual nodes, handles wire changes
// ============================================================================

class VisualNodeCache {
public:
    VisualNodeCache() = default;

    // Get or create visual node for a Node
    BaseVisualNode* getOrCreate(const Node& node);

    // Get visual node by ID
    BaseVisualNode* get(const std::string& node_id);

    // Clear cache
    void clear() { cache_.clear(); }

    // Wire events - call to update visual nodes
    void onWireAdded(const Wire& wire, const std::vector<Node>& all_nodes);
    void onWireDeleted(const Wire& wire, const std::vector<Node>& all_nodes);

private:
    std::unordered_map<std::string, std::unique_ptr<BaseVisualNode>> cache_;

    Node* findNode(const std::string& node_id, std::vector<Node>& nodes);
};
