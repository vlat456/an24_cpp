#pragma once

#include "editor/visual/node/node.h"
#include "editor/data/wire.h"
#include <vector>

namespace an24 {

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

    bool handlePortSwap(const std::string& port_a,
                       const std::string& port_b) override;

    bool swapAliasPorts(const std::string& wire_id_a,
                       const std::string& wire_id_b);

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;

private:
    BusOrientation orientation_;
    std::vector<Wire> wires_;

    void distributePortsInRow(const std::vector<Wire>& wires = {});
    Pt calculatePortPosition(size_t index) const;
    Pt calculateBusSize(size_t port_count) const;
};

} // namespace an24
