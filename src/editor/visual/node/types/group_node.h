#pragma once

#include "editor/visual/node/node.h"

namespace an24 {

class GroupVisualNode : public VisualNode {
public:
    GroupVisualNode(const Node& node);

    RenderLayer renderLayer() const override { return RenderLayer::Group; }
    bool isResizable() const override { return true; }

    bool containsPoint(Pt world_pos) const override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;
};

} // namespace an24
