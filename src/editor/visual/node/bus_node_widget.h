#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "visual/port/visual_port.h"
#include "data/wire.h"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

struct Node;

namespace visual {

/// Bus orientation (wide = horizontal ports on bottom, tall = vertical ports on right).
enum class BusOrientation {
    Horizontal,
    Vertical
};

/// Electrical bus widget (render_hint="bus").
/// Dynamically creates one alias port per connected wire, plus a base "v" port.
/// Ports are distributed evenly along one edge based on orientation.
class BusNodeWidget : public Widget {
public:
    BusNodeWidget(const ::Node& data,
                  BusOrientation orientation = BusOrientation::Horizontal,
                  const std::vector<::Wire>& wires = {});

    std::string_view id() const override { return node_id_; }
    bool isClickable() const override { return true; }

    const std::string& nodeId() const { return node_id_; }
    const std::string& name() const { return name_; }
    BusOrientation orientation() const { return orientation_; }

    /// Resolve a wire's port: bus maps wire_id -> alias port.
    /// If port_name is "v" and wire_id is given, returns the alias port for that wire.
    Port* resolveWirePort(const std::string& port_name,
                          const std::string& wire_id) const;

    /// All ports (alias ports + base "v" port)
    const std::vector<Port*>& ports() const { return ports_; }
    Port* port(const std::string& name) const;
    Port* portByName(std::string_view port_name,
                     std::string_view wire_id = {}) const override;

    /// Wire management: dynamically add/remove alias ports
    void connectWire(const ::Wire& wire);
    void disconnectWire(const ::Wire& wire);

    /// Swap two alias port positions (by wire_id)
    bool swapAliasPorts(const std::string& wire_id_a,
                        const std::string& wire_id_b);

    void setCustomColor(std::optional<uint32_t> c) override { custom_fill_ = c; }
    std::optional<uint32_t> customColor() const override { return custom_fill_; }

    Pt preferredSize(IDrawList* dl) const override;
    void layout(float w, float h) override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;
    void renderPost(IDrawList* dl, const RenderContext& ctx) const override;

private:
    std::string node_id_;
    std::string name_;
    std::string type_name_;

    BusOrientation orientation_;
    std::vector<::Wire> wires_;     ///< Connected wires (for alias port tracking)
    std::vector<Port*> ports_;    ///< Non-owning: alias ports + base "v" port

    std::optional<uint32_t> custom_fill_;

    /// Rebuild all ports from wires_ and recalculate size/positions
    void rebuildPorts();

    /// Calculate bus size based on port count
    Pt calculateBusSize(size_t port_count) const;

    /// Calculate port position by index
    Pt calculatePortLocalPos(size_t index) const;
};

} // namespace visual
