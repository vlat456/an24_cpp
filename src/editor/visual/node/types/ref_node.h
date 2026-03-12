#pragma once

#include "editor/visual/node/node.h"

namespace an24 {

class RefVisualNode : public VisualNode {
public:
    RefVisualNode(const Node& node);

    void recalculatePorts() override;

    void render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
               bool is_selected) const override;
};

} // namespace an24
