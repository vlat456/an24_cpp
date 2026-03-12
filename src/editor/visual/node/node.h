#pragma once

#include "editor/visual/interfaces.h"
#include "editor/data/pt.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "editor/visual/port/port.h"
#include "editor/visual/node/widget/containers/column.h"
#include "editor/visual/node/widget/widget_base.h"
#include "editor/visual/node/bounds.h"
#include "editor/viewport/viewport.h"
#include "json_parser/json_parser.h"
#include <vector>
#include <string>
#include <optional>
#include <memory>

enum class RenderLayer { Group, Text, Node };

struct IDrawList;

class VisualNode : public IDrawable, public ISelectable,
                   public IDraggable, public IPersistable {
public:
    VisualNode(const Node& node);
    virtual ~VisualNode() = default;

    Pt getPosition() const override { return position_; }
    void setPosition(Pt pos) override;
    Pt getSize() const override { return size_; }
    void setSize(Pt size) override;

    const std::string& getId() const override { return node_id_; }

    bool containsPoint(Pt world_pos) const override {
        return world_pos.x >= position_.x && world_pos.x <= position_.x + size_.x &&
               world_pos.y >= position_.y && world_pos.y <= position_.y + size_.y;
    }

    const VisualPort* getPort(const std::string& name) const;
    const VisualPort* getPort(size_t index) const;
    size_t getPortCount() const { return ports_.size(); }
    std::vector<std::string> getPortNames() const;

    virtual const VisualPort* resolveWirePort(const std::string& port_name,
                                               const char* wire_id = nullptr) const;

    virtual void connectWire(const Wire& wire);
    virtual void disconnectWire(const Wire& wire);
    virtual void recalculatePorts();

    virtual bool handlePortSwap(const std::string& port_a,
                               const std::string& port_b) {
        (void)port_a; (void)port_b; return false;
    }

    NodeContentType getContentType() const { return node_content_.type; }
    const NodeContent& getNodeContent() const { return node_content_; }
    Bounds getContentBounds() const;
    virtual void updateNodeContent(const NodeContent& content);

    const Column& getLayout() const { return layout_; }

    const std::optional<NodeColor>& customColor() const { return custom_color_; }
    void setCustomColor(std::optional<NodeColor> c) { custom_color_ = std::move(c); }

    virtual RenderLayer renderLayer() const { return RenderLayer::Node; }
    virtual bool isResizable() const { return false; }

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
    Column layout_;
    std::optional<NodeColor> custom_color_;
    Widget* content_widget_ = nullptr;
    Pt content_offset_;

    struct PortSlot {
         Widget* row_container;
         std::string name;
         bool is_left;
         PortType type;
        float parent_y_offset = 0;
    };
    std::vector<PortSlot> port_slots_;

    void buildLayout(const Node& node);
    void buildPorts(const Node& node);
};
